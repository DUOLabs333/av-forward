package command
import (
	"os"
	"os/exec"
	"fmt"
)

// #cgo LDFLAGS:
/*
int main_test(int argc, char** argv);
typedef int(*program_type)(int,char**);

int call_program(program_type program, int argc, char** argv){
	return program(argc,argv);
}
*/
import "C"
import "unsafe"

var childEnv = "__IS_CHILD"

var entry_points=make(map[string]unsafe.Pointer);

func Register(){ //Run function if you are a child
	if os.Getenv(childEnv) == "" {
		return;
	}
	fmt.Println("Hello");
	var args=os.Args[1:]
	entry_points["ffmpeg"]=C.main_test;

	entry_point,ok:=entry_points[args[0]]
	if (!ok){
		panic(fmt.Sprintf("Command %s not found in available entrypoints!",args[0]))
	}

	argv:=C.malloc(C.size_t(len(args)) * C.size_t(unsafe.Sizeof(uintptr(0))))
	goargv := (*[1<<30 - 1]*C.char)(argv)

	for idx, elem := range args{
		goargv[idx]=C.CString(elem)
	}

	var ret=C.call_program(C.program_type(entry_point),C.int(len(args)),(**C.char)(argv));

	os.Exit(int(ret));
}

func Run(args... string) *exec.Cmd{
	cmd := exec.Command(os.Args[0],args...)
	cmd.Env = append(os.Environ(), fmt.Sprintf("%v=%v", childEnv, 1))
	return cmd
}

