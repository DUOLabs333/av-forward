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
		static constexpr auto value = glz::object(&Device::type, &Device::name, &Device::size);
	};

	std::string buf;

	bp::child process; //Don't know what type Boost.Process uses
	bool started = false;

	std::string cmd(){
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
			process={cmd()};
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
		std::set<AsioConn*> conns;

		int number; //Relative to the video/audio list

		std::vector<AsioConn*> closed_conns;
	#else
		std::atomic<int> num_procs = 0;
	#endif
	


};

std::unordered_map<int, Device> available_devices;



void addConn(int id, AsioConn* conn){ //For server
	auto& device=available_devices[id];

	device.mu.lock();

	device.conns.insert(conn);
	if ((device.conns.size()>=1)){
		device.start();
	}

	device.mu.unlock();
	
	device.cv.notify_one();
}

void removeConn(int id, AsioConn* conn){
	auto& device=available_devices[id];

	device.mu.lock();

	device.conns.erase(conn);
	asio_remove(conn);

	if ((device.conns.size()==0)){
		device.stop();
	}

	device.mu.unlock();
	
	device.cv.notify_one();
}
void handleConn(AsioConn* conn){ //For servers
	char* info;
	int dummy;
	bool err;

	asio_read(conn, &info, &dummy, &err);

	if (err){
		asio_close(conn);
		return;
	}

	if ((uint8_t)info[0]==0){ //Wants to know what devices are available
		auto str=glz::write_json(available_devices);
		asio_write(conn, str.data(), str.size(), &err);
		asio_close(conn);
		return;
	}

	addConn((uint8_t)info[1], conn);

}
//addConn --- gets to 1, start process
//removeConn --- goes to 0, stop process
//When devices are initialized, set string to width*height
//ffmpeg -hide_banner

#ifndef CLIENT
	void handleDevice(int id){ //Server writing to multiple connections at a time
		auto& device=available_devices[id];
		
		for(;;){
			std::unique_lock lk(device.mu);
			device.cv.wait(lk, [&]{return !device.conns.empty();});
			
			device.process.stdout.read(device.buf.data(), device.buf.size()); //I don't know the actual API for Boost::Process


			bool err;
			for(auto& conn : device.conns){
				asio_write(conn, device.buf.data(), device.buf.size(), &err);
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

				device.process.stdin.write(buf, len);

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
		bp::child c (bp::search_path("ffmpeg"),"-f", "avfoundation", "-list_devices", "true", "-i", "\"\"", bp::std_out > bp::null, bp::std_err > is, bp::std_in < bp::null);
		
		int mode=-1;
		
		int id=0;
		for (std::string line; std::getline(is, line);){
			std::string regex_header=R"(\[.+\]\s+)";
			std::regex device_regex(regex_header+R"#(\[(\d+\)]\s+(.+))#");
			if (mode==-1){
				if (std::regex_match(line, std::regex(regex_header+"(AVFoundation video devices))#"))){
				mode=0;
		}else if (std::regex_match(line, std::regex(regex_header+"(AVFoundation audio devices))#"))){
		mode=1;
	}
		continue;
		}else {
			std::smatch match;
			auto matched=std::regex_match(line, device_regex);
			if (!matched){
				continue;
			}
			auto& device=available_devices[id++];
			device.number=stoi(match.str(1));
			device.name=match.str(2);

		}

		}

	#endif

}


