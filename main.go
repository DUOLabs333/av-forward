package main

import (
	"av-forward/lsof"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"strconv"
	"syscall"
	"runtime"
	"strings"
)

var ffmpeg *exec.Cmd
var host *string
var client bool
//Set host based on operating system
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
	var numConn int
	var started bool = true
	var ffmpeg_proc_exists bool
	for true {
		if client{
		pids, _ := lsof.Scan(CheckForVideoFile)
		numConn = len(pids[CAM_FILE])
		}else{
		conns,_ := exec.Command("lsof", "-i", "tcp:8000", "-sTCP:ESTABLISHED", "-t").Output()
		conns=strings.TrimRight(conns, "\n")
		numConn=len(strings.Split(conns, "\n"))
		numConn+=1 //Count the ffmpeg server as a connection to be compatible with the client code
		}
		// fmt.Println(numConn)
		
		if !started {
			ffmpeg_proc_exists, _ = PidExists(ffmpeg.Process.Pid)
		}

		if on && (numConn <= 1 || !ffmpeg_proc_exists) { //Turning off --- only the ffmpeg process is connected or can't connect to the host's camera
			if !started { //When we first start, ffmpeg isn't running, so there's nothing to kill
				ffmpeg.Process.Kill()
				ffmpeg.Process.Wait()
			} else {
				started = false
			}
			
			if client{
			ffmpeg = exec.Command("ffmpeg", "-f", "lavfi", "-i", "color=size=640x480:rate=25:color=black", "-f", "v4l2", CAM_FILE) //Black screen is used as a "turned off"
			//ffmpeg.Stderr=os.Stderr
			ffmpeg.Start()
			waitUntilFileIsOpen(ffmpeg.Process.Pid) //This prevents a bug where we rapidly switch between turning off and on
			}
			on = false

		} else if !on && numConn > 1 { //Turning on
			ffmpeg.Process.Kill()
			ffmpeg.Process.Wait()
			if client{
			ffmpeg = exec.Command("ffmpeg","-fflags", "nobuffer", "-flags", "low_delay", "-strict", "experimental", "-i", fmt.Sprintf("http://%s:8000", *host), "-vf", "format=yuv420p", "-f", "v4l2", CAM_FILE)
			}else{
				ffmpeg = exec.Command("ffmpeg", "-f", "avfoundation", "-framerate", "30", "-i", "0", "-vcodec", "mpeg1video", "-f", "mpegts", "-")
				tail:=exec.Command("stdbuf", "-i0", "-o0", "-e0","tail","-c","+1","-f","/dev/stdin") //Tail buffers, which makes things very slow
				netcat := exec.Command("nc", "-lk", *host, "8000")
				stdout , _ := ffmpeg.StdoutPipe()
				tail.Stdin=stdout
				stdout , _ = tail.StdoutPipe()
				netcat.Stdin=stdout
				tail.Start()
				netcat.Start()
			}
			ffmpeg.Stderr = os.Stderr
			ffmpeg.Start()
			waitUntilFileIsOpen(ffmpeg.Process.Pid) //See previous call
			on = true
		}
	}
}
func main() {
	signal.Ignore(syscall.SIGCHLD) //Prevent zombie children
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
	}
	toggle()
	<-sigs
	ffmpeg.Process.Kill()
}
