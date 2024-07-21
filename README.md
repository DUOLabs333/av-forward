av-forward: Stream webcam between a macOS host and a Linux guest

How to compile:
	Just do `python ../tools/build.py main` in the project directory

How to use:
	On the host:
		* You need ffmpeg installed .
		Run `./av-forward`.

	In the guest:
		* You need ffmpeg, v4l2loopback-dkms, and v4l2loopback-utils installed.
		Run `sudo ./av-forward`.

	That's it! The webcam will automatically turn off and on, depending on whether the guest is actively using it.

Known problems:
	* There is a slight (~10 ms) delay between the host and guest streams. This is very small, but it seems to be unavoidable

	* The quality of the stream in the guest is usable, but isn't great (ie, the guest can only access a compressed version of the stream natively available to the host)
