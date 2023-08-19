package server
import (
	"net"
	"sync"
	"fmt"
)

type Server struct{
	Listener net.Listener
	Conns []net.Conn
	connLock sync.RWMutex
}
func (server *Server) Start(host string, port int){
	var err error
	server.Listener, err=net.Listen("tcp",fmt.Sprintf("%s:%d",host,port))

	if (err!=nil){
		panic(err)
	}
	
	go handleConns(server);
	return
}
	
func handleConns(server *Server){
	for {
		var err error
		conn,err:=server.Listener.Accept()
		if (err!=nil){
			panic(err)
		}
		go handleIncomingRequest(server, conn)
	}
}

func handleIncomingRequest(server *Server,conn net.Conn) {
	server.Add(conn) //Instead of lsof, just keep track of number of connections. Additionally, instead of checking ps -ostat, just go func wait in the background. 
}

func (server *Server) Add(conn net.Conn){
	server.connLock.Lock()
	defer server.connLock.Unlock()
	server.Conns=append(server.Conns,conn)
}

func ReadWriteConns(server *Server, method string, buf []byte) (n int, err error){
	server.connLock.RLock()
	removed:=make([]net.Conn,0)
	n=0
	var numBytes int
	for _, conn :=range server.Conns{
		if method=="Read"{
			numBytes,err = conn.Read(buf)
		}else if method=="Write"{
			numBytes,err = conn.Write(buf)
		}
		if err != nil {
			removed=append(removed,conn)
			continue
		}
		if method=="Read"{
			n+=numBytes
		}else if method=="Write"{
			n=numBytes
		}
	}
	server.connLock.RUnlock()
	server.Remove(removed)
	err=nil
	return 
	
}

func (server *Server) Read(buf []byte) (n int, err error){
	n, err=ReadWriteConns(server,"Read",buf)
	return n, err
}

func (server *Server) Write(buf []byte) (n int, err error){
	n, err=ReadWriteConns(server,"Write",buf)
	return n, err
}

func (server *Server) Remove(conns []net.Conn){
	server.connLock.Lock()
	defer server.connLock.Unlock()
	
	for i := len(server.Conns) - 1; i >= 0; i-- { //We start backwards so that each removal doesn't invalidate the indices in server.Conns before i
		for _, conn := range conns {
			if server.Conns[i] == conn {
				server.Conns = append(server.Conns[:i], server.Conns[i+1:]...)
				conn.Close()
				break
			}
		}
	}
}


func (server *Server) Close() error{
	server.Remove(server.Conns)
	return nil
}

