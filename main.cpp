#include <asio_c.h>
#include <boost/process.hpp>
#include <boost/process/pipe.hpp>
#include <condition_variable>
#include <csignal>
#include <regex>
#include <string_view>
#include <unordered_map>
#include <set>
#include <string>
#include <stdint.h>
#include <glaze/glaze.hpp>
#include <mutex>
#include <thread>
#include <atomic>
#include <regex>

namespace bp = boost::process;

//https://superuser.com/questions/438390/creating-mp4-videos-ready-for-http-streaming
//https://stackoverflow.com/questions/61990828/how-to-redirect-an-audio-stream-to-a-virtual-pulseaudio-microphone-with-ffmpeg
//https://unix.stackexchange.com/questions/651617/write-audio-stream-to-an-alsa-device-with-ffmpeg
//https://superuser.com/questions/1572633/record-application-audio-only-with-ffmpeg-on-macos

auto ffmpeg = bp::search_path("ffmpeg");

enum DeviceType{
	VIDEO=0,
	AUDIO=1
};

typedef struct Device {
	
	DeviceType type;
	std::string name;
	std::array<int,2> size;
	struct glaze {
		static constexpr auto value = glz::object(&Device::type, &Device::name, &Device::size);
	};

	bp::child process;
	bool started = false;

	auto args(){ //Stream video/audio I/O using ffmpeg 
		 if (type==AUDIO){
			#ifdef CLIENT
				return bp::args({""}); //We don't use ffmpeg for this part
			#else
				return bp::args({"-f", "avfoundation", "-i", std::format(":{}",index), "-ar", "16000", "-ac", "1", "-f", "s16le", "-"});
			#endif

		 }else if (type==VIDEO){
			#ifdef CLIENT
				return bp::args({"-f", "mpegts", "-fflags", "nobuffer", "-flags", "low_delay", "-i", "-", "-f", "v4l2", file});
			#else
				return bp::args({"-f", "avfoundation", "-framerate", std::to_string(framerate), "-video_size", std::format("{}x{}", size[0], size[1]), "-i", std::format("{}:", index), "-b:v", "9999k", "-vcodec", "mpeg4", "-f", "mpegts", "-"});
			#endif
		}
	}
	
	void delete_file(){ //This really should be something that runs at startup
		#ifdef CLIENT
			if (type==VIDEO){
				bp::system(std::format("sudo v4l2loopback-ctl delete {}", file));
			}else if (type==AUDIO){
				bp::system(std::format("pactl unload-module {}", module));
			}
		#endif

	}
	
	void start(bool timeout = false){
		std::unique_lock lk(mu);
		if (!started){
			#ifdef CLIENT
				if (type==VIDEO){
					if (timeout){
						process=bp::child(ffmpeg, bp::args({"-f", "lavfi", "-i", std::format("color=size={}x{}:rate={}:color=black", size[0], size[1], 25),"-f", "v4l2", file}));
					}else{
						process=bp::child(ffmpeg, args(), bp::std_in < os);
					}
				} //We don't need to do anything for microphones

			#else
				process=bp::child(ffmpeg, args(), bp::std_out > is);
			#endif
			started=true;
		}

	}
	
	auto& stream(){
		#ifdef CLIENT
			return os;
		#else
			return is;
		#endif

	}
	void stop(){
		mu.lock();
		if(started){
			process.terminate();
			process.wait();
			#ifdef CLIENT
				stream()=bp::opstream();
			#else
				stream()=bp::ipstream();
			#endif

			started=false;
			
		}

		mu.unlock();
	}
	
	auto restart(){ //Only to be used when restarting ffmpeg due to failure
		mu.lock();
		bool restarted=false;
		if(!stream().good()){
			stop();
			start();
			restarted=true;
		}

		mu.unlock();

		return restarted;
	}


	std::recursive_mutex mu;
	std::condition_variable_any cv;

	#ifndef CLIENT
		std::string buf; //For holding the stdout

		bp::ipstream is; //For piping the stdout of ffmpeg to a socket

		std::set<AsioConn*> conns; //Clients that are listening to the server

		int index; //Relative to the video/audio list returned by ffmpeg

		std::vector<AsioConn*> closed_conns; //Temp list to hold connections to be deleted

		int framerate = 0;
	#else
		std::atomic<int> procs_mode = 0; //[a][b] --- [a] whether the current process has the file open. [b] --- whether any other process has it open
		
		bp::opstream os; //For piping from a socket to the stdin of ffmpeg

		std::string file;

		std::string module;

		uint64_t hash;

	#endif
	


} Device;

std::unordered_map<int, Device> available_devices; //All devices available to backend

#ifndef CLIENT
	void addConn(int id, AsioConn* conn){ //For the server
		auto& device=available_devices[id];

		device.mu.lock();

		device.conns.insert(conn);
		if ((device.conns.size()>0)){ //Since there's now someone listening, start the camera to pipe the video to
			device.start();
		}

		device.mu.unlock();
		
		device.cv.notify_one();
	}

	void removeConn(int id, AsioConn* conn){ //For the server
		auto& device=available_devices[id];

		device.mu.lock();

		device.conns.erase(conn);
		asio_close(conn);

		if ((device.conns.size()==0)){
			device.stop();
		}

		device.mu.unlock();
		
		device.cv.notify_one();
	}
	void handleConn(AsioConn* conn){ //For the server
		char* info;
		int dummy; //Even though we know that sizeof(info)==2, asio_read still requires a length parameter
		bool err;

		asio_read(conn, &info, &dummy, &err);

		if (err){
			asio_close(conn);
			return;
		}

		if ((uint8_t)info[0]==0){ //The client wants to know what devices are available
			auto str=glz::write_json(available_devices);
			asio_write(conn, str.data(), str.size(), &err);
			asio_close(conn); //No need for it any more
			return;
		}

		addConn((uint8_t)info[1], conn);

	}
#endif

#ifndef CLIENT
	void handleDevice(int id){ //A thread on the server is writing to multiple connections at a time
		auto& device=available_devices[id];

		device.buf.resize(4096);

		for(;;){
			std::unique_lock lk(device.mu);
			device.cv.wait(lk, [&]{return !device.conns.empty();});
			
			device.is.read(device.buf.data(), device.buf.size()); //We must read instead of readsome as it seems that ffmpeg only writes when a read is initiated
			
			
			if(device.restart()){ //Runs through the loop again if the process has died
				continue;
			}

			bool err;
			for(auto& conn : device.conns){
				asio_write(conn, device.buf.data(), device.is.gcount(), &err);
				if(err){
					device.closed_conns.push_back(conn);
				}
			}

			lk.unlock();

			for(auto& conn: device.closed_conns){
				removeConn(id, conn);
			}
			
			device.closed_conns.clear();

		}
}
#else
	void countOpenHandles(Device& device){ //Polling is a dirty hack, but I'm not sure there's a better way
		for(;;){
			bp::ipstream is;
			device.mu.lock();
			auto pid=device.process.id();
			device.mu.unlock();

			int mode;

			if(device.type==VIDEO){
				mode=0;
				bp::child c(bp::search_path("lsof"), "-t", device.file, bp::std_out > is);

				for(std::string line; is.good() && std::getline(is, line) && !line.empty();){
					mode |= (stoul(line)==pid ? 2 : 1);

					if(mode==3){ //No need to continue, as everything that could be set has been set
						c.terminate();
						break;
					}
				}

				c.wait();
			}else if (device.type==AUDIO){
				mode=2; //This part doesn't matter, as any wait that would depend on this must have the device file open

				bp::ipstream is;
				bp::child c(bp::search_path("pacmd"), "list-source-outputs", bp::std_out > is);

				//TODO: We should use "pactl -f json list source-outputs" instead -- if I have to parse this again, it will be too soon.
				std::regex source_regex(std::format(R"#(\s+source:\s+\d+\s+\<{}\>\s*)#", device.hash));
				for(std::string line; is.good() && std::getline(is, line);){
					if(std::regex_match(line, source_regex) ){
						mode|=1;
						c.terminate();
						break;
					}
				}

			}
			
			device.procs_mode=mode;
			//printf("%i\n", device.procs_mode.load()); //Debug statement
			device.cv.notify_one();

			std::this_thread::sleep_for(std::chrono::seconds(5));
		}
	}

	void handleDevice(int id){
		//Make device as well
		auto& device=available_devices[id];
		uint8_t info[] = {1, id};
		AsioConn* client =NULL;
		bool err;

		char* buf;
		int len;
		
		while(true){ //Creating virtual device. Note that we don't actually check whether the startup commands succeed, but only that the relevant device members are initialized.
			bp::ipstream is;
			if (device.type==VIDEO){
				bp::child c(bp::search_path("sudo"), "v4l2loopback-ctl", "add", "--name", device.name, "--exclusive-caps", "1", bp::std_out > is);

				std::getline(is, device.file);
				c.wait();
				if(is.fail()){
					continue;
				}
			}else if (device.type == AUDIO){
				device.hash=std::hash<std::string>()(device.name);
				device.file=std::format(R"#(/tmp/{}.av.sock)#", device.hash);
				bp::ipstream is;
				bp::child c(bp::search_path("pactl"), "load-module", "module-pipe-source", std::format("source_name={}", device.hash), std::format("file={}",device.file), "format=s16le", "rate=16000", "channels=1", bp::std_out > is);
				std::getline(is, device.module);
				c.wait();
				if(is.fail()){
					continue;
				}
				

				auto escaped_name=std::regex_replace(device.name, std::regex(R"#(\")#"), R"#(\")#");
				bp::system(std::format(R"#(pacmd update-source-proplist {} device.description="{}")#", device.hash, escaped_name));
			}
			break;
		}

		//Call lsof/fuser every 10 s, and update the count
		std::thread watch(countOpenHandles, std::ref(device));
		while(true){ //Runs when client wants to reconnect to server
			asio_close(client);
			device.stop();
			
			device.start(true); //Start timeout process, and show black screen
			
			int fd;
			{
			std::unique_lock lk(device.mu);
			if(device.type==AUDIO){
				while(true){ //Wait for file to become available
					fd=open(device.file.c_str(), O_WRONLY);
					if (fd!=-1){
						break;
					}
				}
 
				fcntl(fd, F_SETPIPE_SZ, PIPE_BUF); //Very important in minimizing delay in the virtual source
				int flags=fcntl(fd, F_GETFL);
				fcntl(fd, F_SETFL,  flags | O_NONBLOCK);

			}
			device.cv.wait(lk, [&](){return (device.procs_mode & 2)==2;}); //Wait for the ffmpeg process to open the file, before checking other processes --- ffmpeg has to write to the file before others can read from the file
			device.cv.wait(lk, [&] { return (device.procs_mode & 1)==1;});
			}

			client=asio_connect(2);

			asio_write(client, (char*)info, sizeof(info), &err);

			if (err){
				continue;
			}
			
			device.stop();
			device.start();
			while(true){
				if ((device.procs_mode & 1) !=1){
					break;
				}
				asio_read(client, &buf, &len, &err);
				if (err){
					break;
				}

				if(device.type==VIDEO){
					device.os.write(buf, len);
				}else if (device.type==AUDIO){ //Ideas taken from here: https://dzx.fr/blog/low-latency-microphone-audio-android/#conclusion
					for(int i=0; i < len; i+=PIPE_BUF){ //Not buffered, which removes most of the latency between host and guest
						write(fd, buf+i, std::min(PIPE_BUF, len-i));
					}
				}

				device.restart(); //Restarts ffmpeg process if its stdin is closed

			}


		}

		watch.join();
	}
#endif

void connectToServer(){
	bool err;
	AsioConn* client = NULL;

	std::vector<std::thread> threads;
	
	uint8_t info[2];
	
	bp::system("sudo modprobe -r v4l2loopback"); //Done because I got tired of manually deleting the devices when the program crashed. Comment this out if you use loopback for something else, as this line will delete all loopback devices

	bp::system("pactl unload-module module-pipe-source"); //See above

	bp::system("sudo modprobe v4l2loopback");

	while (true){
		asio_close(client);
		client=asio_connect(2);

		info[0]=0; //Want to get information about the available devices

		asio_write(client, (char*)info, sizeof(info), &err);

		if(err){
			continue;
		}

		char* buf;
		int len;

		asio_read(client, &buf, &len, &err);

		if (err){
			continue;
		}
		
		glz::read_json(available_devices, std::string_view(buf, len));

		for(auto& [key, val]: available_devices){
			threads.emplace_back(handleDevice, key);
		}

		for(auto& t: threads){
			t.join();
		}
		break;

	}

}

extern "C"{
	void deleteAllFiles(int sig){
		for(auto& [key, val]: available_devices){
			val.delete_file();
			
		}

		if(sig!=0){
			signal(sig, SIG_DFL);
			raise(sig);
		}
	}
}
int main(){

	signal(SIGTERM, deleteAllFiles);
	signal(SIGINT, deleteAllFiles);
	atexit([](){deleteAllFiles(0);});

	#ifdef CLIENT
		connectToServer();
	#else  //Getting all available devices
		bp::ipstream is;
		bp::child c (ffmpeg,"-f", "avfoundation", "-list_devices", "true", "-i", "\"\"", bp::std_out > bp::null, bp::std_err > is, bp::std_in < bp::null);
		
		int mode=-1; //-1 for unknown type, 0 for video devices, 1 for audio devices
		
		int id=0;
		for (std::string line; !is.eof() && std::getline(is, line);){
			std::string regex_header=R"(\[.+\]\s+)";
			std::regex device_regex(regex_header+R"#(\[(\d+)\]\s+(.+))#");
			std::regex size_regex(regex_header+R"#((\d+)x(\d+)@\[\d+\.\d+\s+(\d+\.\d+).+)#");

			constexpr auto device_header_format = "AVFoundation {} devices:";


			if (std::regex_match(line, std::regex(regex_header+std::format(device_header_format,"video")))){
					mode=0;
					continue;
			} else if (std::regex_match(line, std::regex(regex_header+std::format(device_header_format,"audio")))){
					mode=1;
					continue;
			}


			std::smatch match;
			if (!std::regex_match(line, match, device_regex)){
				continue;
			}


			auto& device=available_devices[id];
			device.index=stoi(match.str(1));
			device.name=match.str(2);

			if(device.name.find("Capture screen") != std::string::npos ){ //Should not capture screens
				available_devices.erase(id);
				continue;
			}

			device.type=static_cast<DeviceType>(mode);
			
			if(mode==0){
				bp::ipstream is;
				bp::child c(ffmpeg, "-f", "avfoundation", "-video_size", "1x1", "-i", std::format("{}:", device.index), bp::std_err > is); //This causes an error, which causes ffmpeg to print out the proper resolutions
				for (std::string line; !is.eof() && std::getline(is, line);){
					std::smatch match;
					if(!std::regex_match(line, match, size_regex)){
						continue;
					}

					device.size[0]=stoi(match.str(1));
					device.size[1]=stoi(match.str(2));
					device.framerate=int(stof(match.str(3)));
					break; //Only get the first valid resolution
				}

				c.wait();
			}
			
			
			id++;


		}

		c.wait();
		
		std::vector<std::thread> threads;
		for(auto& [key, val]: available_devices){
			threads.emplace_back(handleDevice, key);
		}
		
		auto server=asio_server_init(2);
		for (;;){
			auto conn=asio_server_accept(server);
			threads.emplace_back(handleConn, conn);
		}
		for(auto& t: threads){
			t.join();
		}

	#endif

}


