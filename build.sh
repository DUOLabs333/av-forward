#FFmpeg commit hash: 8653dcaf7d66
echo "Configuring ffmpeg"
cd FFmpeg
./configure --disable-shared --disable-everything --enable-static --disable-debug --disable-ffprobe --disable-ffplay --enable-indev=avfoundation --enable-muxer=mpegts --enable-decoder=rawvideo --enable-encoder=mpeg1video --enable-protocol=file --enable-protocol=tcp --enable-filter=scale --enable-protocol=pipe

echo "Building static ffmpeg"
make -j8; ar rc libffmpeg.a **/*.o

echo "Build av-forward"
cd ..

GOPROXY=direct CGO_LDFLAGS="FFmpeg/libffmpeg.a -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework CoreGraphics -framework VideoToolbox -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework CoreServices -framework Foundation -lz" go build

echo "Clean ffmpeg build folder"
(cd FFmpeg; make distclean; rm **/*.o **/*.a)