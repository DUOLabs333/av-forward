import subprocess
import argparse,os
parser = argparse.ArgumentParser(description='Forward MacOS camera/microphone to Linux VM')
parser.add_argument(dest="mode",metavar="MODE",choices=["client","server"])
parser.add_argument("-f","--folder",dest="folder",metavar="FOLDER",default=None)

args = parser.parse_args()

if not args.folder:
    if args.mode=="client":
        args.folder="~/Mounts/Macbook"
    else:
        args.folder="~/Services/arch/data/shared"

if args.mode=="server":
    subprocess.run(["sudo","touch",os.path.join(args.folder,'av-forward.camera.socket')])
    subprocess.run(["sudo","ffmpeg", "-f", "avfoundation", "-framerate","30","-i", "0", "-f", "mpegts","-listen","1",f"unix:{os.path.join(args.folder,'av-forward.camera.socket')}"])
else:
    subprocess.run(["sudo","ffmpeg", "-i",f"unix:{os.path.join(args.folder,'av-forward.camera.socket')}", "-vf", "format=yuv420p","-f", "v4l2","/dev/video0"])