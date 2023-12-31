package main

import (
	//"av-forward/command"
	"av-forward/lsof"
	"av-forward/server"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"strconv"
	"syscall"
	"runtime"
	"io"
)

var ffmpeg *exec.Cmd
var stdin io.WriteCloser

var host *string

var client bool
var hostServer *server.Server

const CAM_NUM int = 0

var CAM_FILE string = fmt.Sprintf("/dev/video%d", CAM_NUM)

func CheckForVideoFile(path string) bool {

	return path == CAM_FILE
}

func PidExists(pid int) (bool, error) {
	if pid <= 0 {
		return false, fmt.Errorf("invalid pid %v", pid)
	}
	proc, err := os.FindProcess(int(pid))
	if err != nil {
		return false, err
	}
	err = proc.Signal(syscall.Signal(0))
	if err == nil {
		return true, nil
	}
	if err.Error() == "os: process already finished" {
		return false, nil
	}
	errno, ok := err.(syscall.Errno)
	if !ok {
		return false, err
	}
	switch errno {
	case syscall.ESRCH:
		return false, nil
	case syscall.EPERM:
		return true, nil
	}
	return false, err
}
func waitUntilFileIsOpen(pid int) {
	for true {
		ret, _ := PidExists(pid)
			
		if !ret { //If the process no longer exists, there's no reason to wait
			break
		}
		
		pids := make(map[string][]int)
		lsof.TryReadFdDir([]string{strconv.Itoa(pid)}, CheckForVideoFile, pids, nil)
		if len(pids[CAM_FILE]) > 0 {
			break
		}
	}
}
func toggle() {
	var on bool = true
	var numConns int
	var started bool = true
	var ffmpeg_proc_exists bool
	var stdout io.ReadCloser
	for true {
		if client{
		pids, _ := lsof.Scan(CheckForVideoFile)
		numConns = len(pids[CAM_FILE])
		}else{
		numConns=len(hostServer.Conns)
		
		if !started{ //Count the ffmpeg server as a connection to be compatible with the client code
			numConns+=1
		}
		}
		
		
		if !started && ((!client && on) || (client)) { //FFmpeg is running only if either the host has the camera on, or the client has started running (for the client, a ffmpeg process is running, whether on or off)
			ffmpeg_proc_exists, _ = PidExists(ffmpeg.Process.Pid)
		}

		if on && (numConns <= 1 || !ffmpeg_proc_exists) { //Turning off --- only if the ffmpeg process is connected in the first place or can't connect to the host's camera
			if !started { //When we first start, ffmpeg isn't running, so there's nothing to kill
				ffmpeg.Process.Kill()
			} else {
				started = false
			}
			
			if client{
			ffmpeg = exec.Command("ffmpeg", "-f", "lavfi", "-i", "color=size=1280x720:rate=25:color=black","-f", "v4l2", CAM_FILE) //Black screen is used as a "turned off"
			//ffmpeg.Stderr=os.Stderr
			ffmpeg.Start()
			go ffmpeg.Process.Wait()
			waitUntilFileIsOpen(ffmpeg.Process.Pid) //This prevents a bug where we rapidly switch between turning off and on
			}
			on = false

		} else if !on && numConns > 1 { //Turning on
			if client{
			ffmpeg.Process.Kill()
			ffmpeg = exec.Command("ffmpeg","-fflags", "nobuffer", "-flags", "low_delay", "-strict", "experimental", "-i", fmt.Sprintf("http://%s:8000", *host),"-b:v","1000k", "-vf", "format=yuv420p", "-f", "v4l2", CAM_FILE)
			}else{
				ffmpeg = exec.Command("ffmpeg", "-f", "avfoundation", "-framerate", "30","-video_size","1280x720","-i", "0", "-b:v","1000k", "-vcodec", "mpeg1video", "-f", "mpegts", "-")
				stdout , _ = ffmpeg.StdoutPipe()
			}
			ffmpeg.Stderr = os.Stderr
			ffmpeg.Start()
			go ffmpeg.Process.Wait()
			if !client{
				go io.Copy(stdin,stdout)
			}else{
			waitUntilFileIsOpen(ffmpeg.Process.Pid) //See previous call
			}
			on = true
		}
	}
}
func main() {
	//command.Register()
	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, os.Interrupt, syscall.SIGTERM)
	host=flag.String("host","127.0.0.1","IP where the server will be running on/where the client will be pulling from")
	flag.Parse()
	
	if runtime.GOOS=="darwin"{
		client=false	
	}else if runtime.GOOS=="linux"{
		client=true
	}
	if client {
		exec.Command("modprobe", "-r", "v4l2loopback").Run()
		exec.Command("modprobe", "v4l2loopback", fmt.Sprintf("video_nr=%d", CAM_NUM), `"card_label='Facetime HD Camera"`, "exclusive_caps=1").Run()
		exec.Command("v4l2-ctl", "-d", CAM_FILE, "-c", "timeout=3000").Run()
	}else{
		//netcat := exec.Command("nc", "-lk", *host, "8000")
		//stdin , _ = netcat.StdinPipe()
		//netcat.Start()
		hostServer=new(server.Server)
		hostServer.Start(*host,8000)
		stdin=hostServer
	}
		
	go toggle()
	<-sigs
	ffmpeg.Process.Kill()
}
