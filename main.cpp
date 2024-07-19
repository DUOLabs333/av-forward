#include <asio_c.h>
#include <boost/process.h>
#include <condition_variable>
#include <unordered_map>
#include <set>
#include <string>
#include <stdint.h>
#include <glaze/glaze.hpp>
#include <bit>
#include <mutex>
#include <queue>

//https://superuser.com/questions/438390/creating-mp4-videos-ready-for-http-streaming
//https://stackoverflow.com/questions/61990828/how-to-redirect-an-audio-stream-to-a-virtual-pulseaudio-microphone-with-ffmpeg
//https://unix.stackexchange.com/questions/651617/write-audio-stream-to-an-alsa-device-with-ffmpeg
//https://superuser.com/questions/1572633/record-application-audio-only-with-ffmpeg-on-macos

typedef struct Device {
	enum {VIDEO=0, AUDIO=1} type;
	int number;
	std::set<AsioConn*> conns;
	std::mutex mu;
	std::condition_variable cv;

	std::string name;
	std::pair<int, int> size;
	std::string buf;
	std::vector<AsioConn*> closed_conns;
	
	std::any process; //Don't know what type Boost.Process uses
	bool started = false;
	struct glaze {
		using T = Device;
		static constexpr auto value = glz::object(&T::type, &T::number, &T::name, &T::size);
	};

};

std::unordered_map<int, Device> id_to_device;



void addConn(int id, AsioConn* conn){ //For server
	auto& device=id_to_device[id];

	device.mu.lock();

	device.conns.insert(conn);
	if ((device.conns.size()>=1) && !device.started){
		device.process.start();
		device.started=true;
	}

	device.mu.unlock();
	
	device.cv.notify_one();
}

void removeConn(int id, AsioConn* conn){
	auto& device=id_to_device[id];

	device.mu.lock();

	device.conns.erase(conn);
	if ((device.conns.size()==1) && device.started){
		device.process.stop();
		device.process.wait();
		device.started=false;
	}

	device.mu.unlock();
	
	device.cv.notify_one();
}
void handleConn(AsioConn* conn){ //For servers
	uint8_t info[2];

	bool err;

	asio_read(conn, info, sizeof(info), &err);

	if (err){
		asio_close(conn);
		return;
	}

	if (info[0]==0){ //Wants to know what devices are available
		auto str=glz::write_json(available_devices);
		asio_write(conn, str.data(), str.size(), &err);
		asio_close(conn);
		return;
	}

	addConn(info[1], conn);

}
//addConn --- gets to 1, start process
//removeConn --- goes to 0, stop process
//When devices are initialized, set string to width*height
//ffmpeg -hide_banner
void handleDevice(int id){ //Server writing to multiple connections at a time
	auto& device=id_to_device[id];
	
	for(;;){
		std::unique_lock lk(device.mu);
		device.cv.wait(lk, [&]{return !device.conns.empty();});
		lk.unlock();
		
		device.process.read(device.buf.data(), device.buf.size()); //I don't know the actual API for Boost::Process
		
		lk.lock();

		bool err;
		for(auto& conn : device.conns){
			asio_read(conn, device.buf.data(), device.buf.size(), &err);
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
	
	





