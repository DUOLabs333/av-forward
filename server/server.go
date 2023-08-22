package server
import (
	"net"
	"sync"
	"fmt"
	"time"
)

type Server struct{
	Listener net.Listener
	Conns []net.Conn
	connLock sync.RWMutex
	closed map[net.Conn]bool
	closedLock sync.RWMutex
}
func (server *Server) Start(host string, port int){
	var err error
	server.Listener, err=net.Listen("tcp",fmt.Sprintf("%s:%d",host,port))

	server.closed=make(map[net.Conn]bool)

	if (err!=nil){
		panic(err)
	}
	
	go handleConns(server);
	go removeClosedConns(server);
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

func removeClosedConns(server *Server){
	for {
		server.closedLock.RLock()
		if len(server.closed)==0{
			server.closedLock.RUnlock()
			continue
		}
		server.closedLock.RUnlock()

		server.closedLock.Lock()
		server.connLock.Lock()
		for i := len(server.Conns) - 1; i >= 0; i-- { //We start backwards so that each removal doesn't invalidate the indices in server.Conns before i
			for conn, _ := range server.closed {
				if server.Conns[i] == conn {
					server.Conns = append(server.Conns[:i], server.Conns[i+1:]...)
					delete(server.closed,conn)
					conn.Close()
					break
				}
			}
		}
		server.closedLock.Unlock()
		server.connLock.Unlock()

	}
}
func (server *Server) Add(conn net.Conn){
	server.connLock.Lock()
	defer server.connLock.Unlock()

	server.Conns=append(server.Conns,conn)
}



func ReadWriteConns(server *Server, method string, buf []byte) (n int, err error){
	server.connLock.RLock()
	
	defer server.connLock.RUnlock()

	n=0
	var numBytes int
	
	for _, conn :=range server.Conns{
		if method=="Read"{
			conn.SetReadDeadline(time.Now().Add(5 * time.Second))
			numBytes,err = conn.Read(buf)
			
		}else if method=="Write"{
			numBytes,err = conn.Write(buf)
		}
		
		
		if err != nil {
			netErr, ok := err.(net.Error)
			if !(ok && netErr.Timeout()){ //If not a timeout, then assume I/O error and the connection is closed
				server.Remove(conn)
				continue
			}
		}

		if method=="Read"{
			n+=numBytes
		}else if method=="Write"{
			n=numBytes
		}
	}

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

func (server *Server) Remove(conns ...net.Conn){
	for _, conn := range conns{
		server.closedLock.RLock()
		_,ok := server.closed[conn]
		server.closedLock.RUnlock()
		if !ok{
			server.closedLock.Lock()
			server.closed[conn]=true
			server.closedLock.Unlock()
		}
	}
}


func (server *Server) Close() error{
	server.Listener.Close()
	return nil
}

