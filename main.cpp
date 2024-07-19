#include <asio_c.h>
#include <boost/process.h>
#include <boost/process/io.hpp>
#include <condition_variable>
#include <glaze/json/read.hpp>
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
#include <boost/process.hpp>
#include <regex>

namespace bp = boost::process;

//https://superuser.com/questions/438390/creating-mp4-videos-ready-for-http-streaming
//https://stackoverflow.com/questions/61990828/how-to-redirect-an-audio-stream-to-a-virtual-pulseaudio-microphone-with-ffmpeg
//https://unix.stackexchange.com/questions/651617/write-audio-stream-to-an-alsa-device-with-ffmpeg
//https://superuser.com/questions/1572633/record-application-audio-only-with-ffmpeg-on-macos

typedef struct Device {
	
	enum {VIDEO=0, AUDIO=1} type;
	std::string name;
	std::array<int,2> size;
	struct glaze {
		static constexpr auto value = glz::object(&Device::type, &Device::name);
	};

	bp::child process;
	bool started = false;

	auto cmd(){
		 if (type==AUDIO){
			#ifdef CLIENT
			#else
			#endif
		 }else if (type==VIDEO){
			#ifdef CLIENT
			#else
			#endif
		}
	}

	void start(){
		if (!started){
			process=cmd();
			started=true;
		}
	}

	void stop(){
		if(started){
			process.terminate();
			process.wait();
			started=false;
			
		}
	}

	std::mutex mu;
	std::condition_variable cv;

	#ifndef CLIENT
		std::string buf; //For holding the stdout

		bp::ipstream is; //For piping the stdout of ffmpeg to a socket

		std::set<AsioConn*> conns; //Clients that are listening to the server

		int number; //Relative to the video/audio list returned by ffmpeg

		std::vector<AsioConn*> closed_conns; //Temp list to hold connections to be deleted
	#else
		std::atomic<int> num_procs = 0; //Number of programs that actively have the file open
		
		bp::opstream os; //For piping from a socket to the stdin of ffmpeg

	#endif
	


};

std::unordered_map<int, Device> available_devices; //All devices available to proxy

auto ffmpeg = bp::search_path("ffmpeg");

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

#ifndef CLIENT
	void handleDevice(int id){ //A thread on the server is writing to multiple connections at a time
		auto& device=available_devices[id];

		device.buf.resize(device.size[0]*device.size[1]);

		for(;;){
			std::unique_lock lk(device.mu);
			device.cv.wait(lk, [&]{return !device.conns.empty();});
			
			device.is.readsome(device.buf.data(), device.buf.size());


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
	void handleDevice(int id){
		//Make device as well
		auto& device=available_devices[id];
		uint8_t info[] = {1, id};
		AsioConn* client =NULL;
		bool err;

		char* buf;
		int len;
		
		//Call lsof/fuser every 10 s
		std::thread watch(countOpenHandles, std::ref(device));
		while(true){
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
				asio_write(client, &buf, &len, &err);
				if (err){
					break;
				}

				device.os.write(buf, len);

			}


		}

		w.join();
	}
#endif

void connectToServer(){
	bool err;
	AsioConn* client = NULL;

	std::vector<std::thread> threads;
	
	uint8_t info[2];
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
			threads.emplace(handleDevice, key);
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
	#else
		bp::ipstream is;
		bp::child c (ffmpeg,"-f", "avfoundation", "-list_devices", "true", "-i", "\"\"", bp::std_out > bp::null, bp::std_err > is, bp::std_in < bp::null);
		
		int mode=-1;
		
		int id=0;
		for (std::string line; !is.eof() && std::getline(is, line);){
			std::string regex_header=R"(\[.+\]\s+)";
			std::regex device_regex(regex_header+R"#(\[(\d+\)]\s+(.+))#");
			if (mode==-1){
				if (std::regex_match(line, std::regex(regex_header+"(AVFoundation video devices))#"))){
					mode=static_cast<int>(decltype(Device::type)::VIDEO);
				} else if (std::regex_match(line, std::regex(regex_header+"(AVFoundation audio devices))#"))){
					mode=static_cast<int>(decltype(Device::type)::AUDIO);
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
				device.number=stoi(match.str(1));
				device.name=match.str(2);

				if(device.name.substr("Capture screen")){ //Should not capture screens
					available_devices.erase(id);
					continue;
				}

				device.type=static_cast<decltype(Device::type)>(mode);
				
				bp::child c(ffmpeg, "-f", "avfoundation", "-video_size", "1x1", "-i", std::format("{}:", device.number));
				for (std::string line; !is.eof() && std::getline(is, line);){
					std::smatch match;
					std::regex_match(line, match, std::regex(regex_header+R"#((\d+)x(\d+)@)#"));
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
		
		std::vector<std::thread>
		for(auto& [key, val]: available_devices){
			threads.emplace(handleDevice, key);
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


