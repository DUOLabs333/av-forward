av-forward: Stream webcam between MacOS host and Linux guest

How to compile:
	Just do `go build` in the project directory

How to use:
	In the host:
		You need ffmpeg.
		Run `./av-forward -host $HOST`, where $HOST is the IP address where you want to run the forwarding server from.

	In the guest:
		You need ffmpeg, v4l2-ctl, and v4l2loopback.
		Run `sudo ./av-forward -host $HOST`, where $HOST is the IP address you chose before.

	That's it! The webcam will automatically turn off and on, depending on whether the guest is actively using it.

Known problems:
	* There is a slight delay between the host and guest streams.

	* The quality of the stream in the guest is usable, but isn't great

	* (Potential) External dependencies can easily be statically linked in.
		In this case, I already explored this and even set up the infrastructure to do this with any needed binary (evident in source code). However, I eventually decided there was no need for it. However, if it does become a problem, I'll look into statically linking dependencies.
