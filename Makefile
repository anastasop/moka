
srv: srv.cc srvhttp.cc srvhttp.h
	g++ `pkg-config --cflags libglog libgflags` -o srv srv.cc srvhttp.cc `pkg-config --libs libglog libgflags`

clean:
	@rm -f srv srv.core
