#FFmpeg commit hash: 8653dcaf7d66

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
	AR="ar rcP"
else
	AR="ar rc"
fi

echo "Configuring ffmpeg"
cd external/FFmpeg
./configure --disable-shared --disable-everything --enable-static --disable-debug --disable-ffprobe --disable-ffplay --disable-sdl2 --disable-bzlib --disable-xlib --disable-zlib --disable-lzma --disable-libxcb --disable-alsa --disable-iconv --disable-vdpau --disable-vaapi --disable-vulkan --disable-v4l2_m2m --enable-indev=avfoundation --enable-outdev=v4l2 --enable-muxer=mpegts --enable-decoder=rawvideo --enable-encoder=mpeg1video --enable-protocol=file --enable-protocol=tcp --enable-filter=scale --enable-protocol=pipe

echo "Building static ffmpeg"
make -j8; $AR ../libffmpeg.a **/**/*.o

echo "Clean build folder"
make distclean; rm **/*.o **/*.a

CGO_LD_FLAGS="external/libffmpeg.a"
if [[ "$OSTYPE" == "darwin"* ]]; then
	CGO_LD_FLAGS=$CGO_LD_FLAGS" -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework CoreGraphics -framework VideoToolbox -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework CoreServices -framework Foundation -lz"
fi

echo "Build av-forward"
cd ..

CGO_LDFLAGS=$CGO_LD_FLAGS" -lm"
GOPROXY=direct CGO_LDFLAGS=$CGO_LDFLAGS go build