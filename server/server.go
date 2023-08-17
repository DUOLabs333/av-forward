package server
import (
	"net"
	"io"
	"sync"
	"fmt"
)

type Server struct{
	Listener net.Listener
	Writer *MultiWriter
}
func (server *Server) Start(host string, port int){
	var err error
	server.Listener, err=net.Listen("tcp",fmt.Sprintf("%s:%d",host,port))

	if (err!=nil){
		panic(err)
	}
	
	server.Writer=New(io.Discard);
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
	server.Writer.Add(conn)
}


// MultiWriter is a writer that writes to multiple other writers.
type MultiWriter struct {
	sync.RWMutex
	writers []io.Writer
}

// New creates a writer that duplicates its writes to all the provided writers,
// similar to the Unix tee(1) command. Writers can be added and removed
// dynamically after creation.
//
// Each write is written to each listed writer, one at a time. If a listed
// writer returns an error, remove from the list and continue
func New(writers ...io.Writer) *MultiWriter {
	mw := &MultiWriter{writers: writers}
	return mw
}

// Write writes some bytes to all the writers.
func (mw *MultiWriter) Write(p []byte) (n int, err error) {
	//fmt.Printf("Num of writers: %d\n",len(mw.writers))
	mw.RLock()
	defer mw.RUnlock()

	for _, w := range mw.writers {
		n, err = w.Write(p)
		if err != nil {
			mw.Remove(w)
			continue
		}

		if n < len(p) {
			err = io.ErrShortWrite
			return
		}
	}

	return len(p), nil
}

// Add appends a writer to the list of writers this multiwriter writes to.
func (mw *MultiWriter) Add(w io.Writer) {
	mw.Lock()
	mw.writers = append(mw.writers, w)
	mw.Unlock()
}

// Remove will remove a previously added writer from the list of writers.
func (mw *MultiWriter) Remove(w io.Writer) {
	mw.RLock()
	var writers []io.Writer
	for _, ew := range mw.writers {
		if ew != w {
			writers = append(writers, ew)
		}
	}
	mw.writers = writers
	mw.RUnlock()
}

func (mw *MultiWriter) Close() error{
	for _, ew := range mw.writers {
		mw.Remove(ew)
	}
	return nil
}