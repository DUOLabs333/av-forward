package main
import (
"flag"
"os/exec"
"fmt"
"os"
"os/signal"
"syscall"
"strconv"
"av-forward/lsof"
)
var ffmpeg *exec.Cmd
var host *string
const CAM_NUM int=0
var CAM_FILE string = fmt.Sprintf("/dev/video%d",CAM_NUM)

func CheckForVideoFile(path string) bool {

	return path==CAM_FILE
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
func waitUntilFileIsOpen(pid int){
	for(true){
	ret,_:=PidExists(pid)
	if (!ret){
		break
	}
	pids:=make(map[string][]int)
	lsof.TryReadFdDir([]string{strconv.Itoa(pid)},CheckForVideoFile,pids,nil)
	if (len(pids[CAM_FILE])>0){
		break
	}
	}
}
func toggle_client(){
var on bool=true
var numPIDs int
var started bool=true
for(true){
pids,_:=lsof.Scan(CheckForVideoFile)
numPIDs=len(pids[CAM_FILE])
//fmt.Println(numPIDs)
var ffmpeg_exists bool
if (!started){
	ffmpeg_exists,_=PidExists(ffmpeg.Process.Pid)
}
if(on && (numPIDs<=1 || !ffmpeg_exists)){
	if (!started){
		ffmpeg.Process.Kill()
		ffmpeg.Process.Wait()
		fmt.Println("ON")
	}else{
		started=false
	}
	ffmpeg =exec.Command("ffmpeg","-f", "lavfi", "-i", "color=size=640x480:rate=25:color=black","-f","v4l2",CAM_FILE)
	//ffmpeg.Stderr=os.Stderr
	ffmpeg.Start()
	on=false
	waitUntilFileIsOpen(ffmpeg.Process.Pid)
	
}else if (!on && numPIDs>1){
	ffmpeg.Process.Kill()
	ffmpeg.Process.Wait()
	ffmpeg =exec.Command("ffmpeg", "-fflags", "nobuffer", "-flags", "low_delay",  "-strict", "experimental","-i",fmt.Sprintf("http://%s:8000",*host), "-vf", "format=yuv420p","-f", "v4l2",CAM_FILE)
	ffmpeg.Stderr=os.Stderr
	ffmpeg.Start()
	on=true
	waitUntilFileIsOpen(ffmpeg.Process.Pid)
}
}
}
func main(){
signal.Ignore(syscall.SIGCHLD) //Prevent zombie children
sigs := make(chan os.Signal, 1)
signal.Notify(sigs, os.Interrupt,syscall.SIGTERM)
mode:=flag.String("mode","","Run either client or server")
host=flag.String("host","127.0.0.1","IP where the server will be running on/where the client will be pulling from")
flag.Parse()
if (*mode=="server"){
	var ffmpeg=exec.Command("sudo","ffmpeg", "-f", "avfoundation", "-framerate","30","-i", "0","-vcodec","mpeg1video","-f", "mpegts","-")
	var netcat=exec.Command("sudo","nc","-lk",*host,"8000")
	ffmpeg_pipe, _ := ffmpeg.StdoutPipe()
	netcat.Stdin=ffmpeg_pipe
	ffmpeg.Stderr=os.Stderr
	ffmpeg.Start()
	netcat.Run()
}else{
	exec.Command("modprobe", "-r", "v4l2loopback").Run()
	var test=exec.Command("modprobe", "v4l2loopback", fmt.Sprintf("video_nr=%d",CAM_NUM), "card_label='Facetime HD Camera'", "exclusive_caps=1")
	test.Stderr=os.Stderr
	test.Run()
	exec.Command("v4l2-ctl", "-d", CAM_FILE, "-c", "timeout=3000").Run()
	go toggle_client()
	<-sigs
	ffmpeg.Process.Kill()
}
}