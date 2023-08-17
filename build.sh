#FFmpeg commit hash: 8653dcaf7d66
echo "Configuring ffmpeg"
cd external/FFmpeg
./configure --disable-shared --disable-everything --enable-static --disable-debug --disable-ffprobe --disable-ffplay --enable-indev=avfoundation --enable-muxer=mpegts --enable-decoder=rawvideo --enable-encoder=mpeg1video --enable-protocol=file --enable-protocol=tcp --enable-filter=scale --enable-protocol=pipe

echo "Building static ffmpeg"
make -j8; ar rc ../libffmpeg.a **/*.o

echo "Clean build folder"
make distclean; rm **/*.o **/*.a

if [[ "$OSTYPE" == "darwin"* ]]; then
	echo "Configuring lsof"
	cd ../lsof
	./Configure

	echo "Building static lsof"
	make -j8; ar rc ../liblsof.a **/*.o

	echo "Clean build folder"
	make distclean; rm **/*.o **/*.a
fi
echo "Build av-forward"
cd ..

GOPROXY=direct CGO_LDFLAGS="external/libffmpeg.a -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework CoreGraphics -framework VideoToolbox -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework CoreServices -framework Foundation -lz external/liblsof.a" go build