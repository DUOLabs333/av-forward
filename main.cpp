#include <asio_c.h>
#include <boost/process.hpp>
#include <condition_variable>
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
		static constexpr auto value = glz::object(&Device::type, &Device::name);
	};

	bp::child process;
	bool started = false;

	auto args(){ //Stream video/audio I/O using ffmpeg 
		 if (type==AUDIO){
			#ifdef CLIENT
			#else
			#endif
		 }else if (type==VIDEO){
			#ifdef CLIENT
				return bp::args({"-f", "mpegts", "-fflags", "nobuffer", "-flags", "low_delay", "-i", "-", "-f", "v4l2", file});
			#else
				return bp::args({"-f", "avfoundation", "-framerate", std::to_string(framerate), "-video_size", std::format("{}x{}", size[0], size[1]), "-i", std::format("{}:", index), "-vcodec", "mpeg4", "-f", "mpegts", "-"});
			#endif
		}
	}

	void start(){
		mu.lock();
		if (!started){
			#ifdef CLIENT
				process=bp::child(ffmpeg, args(), bp::std_in < os);
			#else
				process=bp::child(ffmpeg, args(), bp::std_out > is);
			#endif
			started=true;
		}

		mu.unlock();

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
			stream().clear();
			started=false;
			
		}

		mu.unlock();
	}
	
	auto restart(){
		mu.lock();
		bool restarted=false;
		if(!stream().good()){
			stop();
			start();
			stream().clear();
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
		std::atomic<int> num_procs = 0; //Number of programs that actively have the file open
		
		bp::opstream os; //For piping from a socket to the stdin of ffmpeg

		std::string file;

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

		device.buf.resize(device.size[0]*device.size[1]);

		for(;;){
			std::unique_lock lk(device.mu);
			device.cv.wait(lk, [&]{return !device.conns.empty();});
			
			device.is.readsome(device.buf.data(), device.buf.size());
			
			
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
			int procs=0;
			bp::child c(bp::search_path("lsof"), "-t", device.file, bp::std_out > is);

			for(std::string line; is.good() && std::getline(is, line) && !line.empty();){
				procs++;
			}

			c.wait();

			device.num_procs=procs;

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
		
		device.file=std::format("/dev/video{}", id);
		if (device.type==VIDEO){ //Creating virtual device
			bp::system(bp::search_path("sudo"), "modprobe", "v4l2loopback", std::format("video_nr={}", id), std::format("card_label={}", device.name), "exclusive_caps=1");
		}
		//Call lsof/fuser every 10 s, and update the count
		std::thread watch(countOpenHandles, std::ref(device));
		while(true){ //Runs when client wants to reconnect to server
			asio_close(client);
			device.stop();
			
			std::unique_lock lk(device.mu);
			device.cv.wait(lk, [&] { return device.num_procs > 0;});

			client=asio_connect(2);

			asio_write(client, (char*)info, sizeof(info), &err);

			if (err){
				continue;
			}
			
			device.start();
			while(true){
				if (device.num_procs==0){
					break;
				}
				asio_read(client, &buf, &len, &err);
				if (err){
					break;
				}

				device.os.write(buf, len);

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

	bp::system("modprobe -r v4l2loopback");

	while (true){
		asio_close(client);
		client=asio_connect(2);

		info[0]=0; //Want to get infotmation about the available devices

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
	

int main(){

	#ifdef CLIENT
		connectToServer();
	#else  //Getting all available devices
		bp::ipstream is;
		bp::child c (ffmpeg,"-f", "avfoundation", "-list_devices", "true", "-i", "\"\"", bp::std_out > bp::null, bp::std_err > is, bp::std_in < bp::null);
		
		int mode=-1;
		
		int id=0;
		for (std::string line; !is.bad() && std::getline(is, line);){
			std::string regex_header=R"(\[.+\]\s+)";
			std::regex device_regex(regex_header+R"#(\[(\d+\)]\s+(.+))#");
			if (mode==-1){
				if (std::regex_match(line, std::regex(regex_header+"(AVFoundation video devices))#"))){
					mode=static_cast<int>(DeviceType::VIDEO);
				} else if (std::regex_match(line, std::regex(regex_header+"(AVFoundation audio devices))#"))){
					mode=static_cast<int>(DeviceType::AUDIO);
				}
				continue;
			
			} else {
				std::smatch match;
				auto matched=std::regex_match(line, device_regex);
				if (!matched){
					continue;
				}

				if(mode==1){
					continue; //Just for now, until I figure out how to get audio working (use nc as a makeshift server)
				}
				auto& device=available_devices[id];
				device.index=stoi(match.str(1));
				device.name=match.str(2);
				device.framerate=int(stof(match.str(3));

				if(device.name.find("Capture screen") != std::string::npos ){ //Should not capture screens
					available_devices.erase(id);
					continue;
				}

				device.type=static_cast<DeviceType>(mode);
				
				bp::child c(ffmpeg, "-f", "avfoundation", "-video_size", "1x1", "-i", std::format("{}:", device.index)); //This causes an error, which causes ffmpeg to print out the proper resolutions
				for (std::string line; !is.eof() && std::getline(is, line);){
					std::smatch match;
					std::regex_match(line, match, std::regex(regex_header+R"#((\d+)x(\d+)@\[\d+\.\d+\s+(\d+\.\d+).+)#"));
					if(!match){
						continue;
					}

					device.size[0]=stoi(match.str(1));
					device.size[1]=stoi(match.str(2));
				}

				c.wait();
				
				
				id++;

			}

		}

		c.wait();
		
		std::vector<std::thread> threads;
		for(auto& [key, val]: available_devices){
			threads.emplace_back(handleDevice, key);
		}
		
		auto server=asio_server_init(2);
		for (;;){
			auto conn=asio_server_connect(server);
			threads.emplace(handleConn, conn);
		}
		for(auto& t: threads){
			t.join();
		}

	#endif

}


