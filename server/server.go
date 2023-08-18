package server
import (
	"net"
	"io"
	"sync"
	"fmt"
	"bufio"
)

type Server struct{
	Listener net.Listener
	Writer *MultiWriter
	Reader *MultiReader
}
func (server *Server) Start(host string, port int){
	var err error
	server.Listener, err=net.Listen("tcp",fmt.Sprintf("%s:%d",host,port))

	if (err!=nil){
		panic(err)
	}
	
	server.Writer=NewMW(io.Discard);
	server.Reader=NewMR(io.NopCloser(bufio.NewReader(nil)));
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
	server.Reader.Add(conn) //Maybe check OFF/ON instead of lsof checking ports. Additionally, instead of checking ps -ostat, just go func wait in the background
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
func NewMW(writers ...io.Writer) *MultiWriter {
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


// MultiReader is a reader that reads from multiple other readers.
type MultiReader struct {
	sync.RWMutex
	readers []io.Reader
}


// Each read is read from each listed reader, one at a time. If a listed
// reader returns an error, remove from the list and continue
func NewMR(readers ...io.Reader) *MultiReader {
	mr := &MultiReader{readers: readers}
	return mr
}

// Write reads some bytes from all the readers.
func (mr *MultiReader) Read(p []byte) (n int, err error) {
	//fmt.Printf("Num of readers: %d\n",len(mr.readers))
	mr.RLock()
	defer mr.RUnlock()

	for _, w := range mr.readers {
		n, err = w.Read(p)
		if err != nil {
			mr.Remove(w)
			continue
		}

		if n < len(p) {
			err = io.ErrUnexpectedEOF
			return
		}
	}

	return len(p), nil
}

// Add appends a reader to the list of readers this multireader reads to.
func (mr *MultiReader) Add(w io.Reader) {
	mr.Lock()
	mr.readers = append(mr.readers, w)
	mr.Unlock()
}

// Remove will remove a previously added reader from the list of readers.
func (mr *MultiReader) Remove(w io.Reader) {
	mr.RLock()
	var readers []io.Reader
	for _, ew := range mr.readers {
		if ew != w {
			readers = append(readers, ew)
		}
	}
	mr.readers = readers
	mr.RUnlock()
}

func (mr *MultiReader) Close() error{
	for _, ew := range mr.readers {
		mr.Remove(ew)
	}
	return nil
}