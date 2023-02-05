import subprocess
import argparse,os
parser = argparse.ArgumentParser(description='Forward MacOS camera/microphone to Linux VM')
parser.add_argument(dest="mode",metavar="MODE",choices=["client","server"])
parser.add_argument("-f","--folder",dest="folder",metavar="FOLDER",default=None)

args = parser.parse_args()

if not args.folder:
    if args.mode=="client":
        args.folder=os.path.expanduser("~/Mounts/Macbook")
    else:
        args.folder=os.path.expanduser("~/Services/arch/data/shared")

if args.mode=="server":
    subprocess.run(["sudo","rm","-f",os.path.join(args.folder,"av-forward.camera.socket")])
    with subprocess.Popen(["sudo","ffmpeg", "-f", "avfoundation", "-framerate","30","-i", "0", "-f", "mpegts","-"],stdout=subprocess.PIPE) as process:
        subprocess.run(["sudo","nc","-Ulk",os.path.join(args.folder,"av-forward.camera.socket")],stdin=process.stdout)
    #os.system(f'ffmpeg -f avfoundation -framerate 30 -i "0" -f mpegts - | sudo nc -Ulk {os.path.join(args.folder,"av-forward.camera.socket")}')
else:
    subprocess.run(["sudo","ffmpeg", "-i",f"unix:{os.path.join(args.folder,'av-forward.camera.socket')}", "-vf", "format=yuv420p","-f", "v4l2","/dev/video0"])