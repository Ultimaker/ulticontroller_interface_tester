# UltiController Interface Tester

The UltiController Interface Tester or ucit for short, is a simple optionally
statically linkable application that can be used to validate the touch-screen
available on the UltController.

The program has several tuneables as command line parameters and arguments. See
ucit --help for details.


## Compiling
To compile the application a regular cmake construct is available easily invoked
using the available
```sh
build_for_ultimaker.sh
```
which uses docker if available. This will only however create an armhf package.

If a different build environment is needed, this can be accomplished by first
created the docker container
```sh
docker build -t "ucit:latest" .
```
and then supply this image to the build script
```sh
CI_REGISTRY_IMAGE="ucit:latest" ./build_for_ultimaker.sh
```

Alternatively calling the build script manually, it is possible to create an
amd64 build. Unfortunately the docker container can currently not be used for
this yet.
First build sources (the -c 'Skip cleanup' flag is required)
```sh
ARCH=amd64 ./build.sh -c
```
and then run the binary from the build directory.
```
.build_amd64/ucit <arguments>
```
Note however that while the application will run, no output will be visible if
the framebuffer is not actually available. This is common when using a desktop
operating system.

# Known issues
* During startup it may happen that the previous touch event is still active.
  This appears to be a bug in the firmware, which the driver should try to
  resolve.
* Performance has at times be found to be abysmal at times, this was very
  noticeable when enabling both the banding option and the fade.
* Fading does not look properly when banding due to the XOR.
* On a black background, no banding is possible.
* When setting the -a flag to abort the test on successful touch test, the
  last square is not rendered.
