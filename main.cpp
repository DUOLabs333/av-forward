#include <asio_c.h>
#include <boost/process.h>
#include <condition_variable>
#include <glaze/json/read.hpp>
#include <string_view>
#include <unordered_map>
#include <set>
#include <string>
#include <stdint.h>
#include <glaze/glaze.hpp>
#include <mutex>
#include <thread>

//https://superuser.com/questions/438390/creating-mp4-videos-ready-for-http-streaming
//https://stackoverflow.com/questions/61990828/how-to-redirect-an-audio-stream-to-a-virtual-pulseaudio-microphone-with-ffmpeg
//https://unix.stackexchange.com/questions/651617/write-audio-stream-to-an-alsa-device-with-ffmpeg
//https://superuser.com/questions/1572633/record-application-audio-only-with-ffmpeg-on-macos

typedef struct Device {
	
	enum {VIDEO=0, AUDIO=1} type;
	std::string name;
	std::pair<int, int> size;
	struct glaze {
		using T = Device;
		static constexpr auto value = glz::object(&T::type, &T::name, &T::size);
	};

	std::string buf;

	std::any process; //Don't know what type Boost.Process uses
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

	#ifndef CLIENT
		std::set<AsioConn*> conns;
		std::mutex mu;
		std::condition_variable cv;

		int number; //Relative to the video/audio list

		std::vector<AsioConn*> closed_conns;
	#endif
	


};

std::unordered_map<int, Device> available_devices;



void addConn(int id, AsioConn* conn){ //For server
	auto& device=available_devices[id];

	device.mu.lock();

	device.conns.insert(conn);
	if ((device.conns.size()>=1) && !device.started){
		device.process=device.cmd().start();
		device.started=true;
	}

	device.mu.unlock();
	
	device.cv.notify_one();
}

void removeConn(int id, AsioConn* conn){
	auto& device=available_devices[id];

	device.mu.lock();

	device.conns.erase(conn);
	asio_remove(conn);

	if ((device.conns.size()==0) && device.started){
		device.process.stop();
		device.process.wait();
		device.started=false;
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
			lk.unlock();
			
			device.process.read(device.buf.data(), device.buf.size()); //I don't know the actual API for Boost::Process
			
			lk.lock();

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
		auto& device=available_devices[id];
		uint8_t info[2];

		while(true){
			auto client
		}
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
	
	





