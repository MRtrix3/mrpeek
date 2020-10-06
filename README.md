# `mrpeek`: a medical image viewer in the terminal

Launched as a project for the [OHBM BrainHack 2020](https://ohbm.github.io/hackathon2020/), `mrpeek` allows to quickly inspect 3-D and 4-D medical images without leaving the terminal. Demo [here](https://twitter.com/jdtournier/status/1273657837034655744?s=20).

> **NOTE:** this currently compiles and runs on GNU/Linux & macOS systems, but
> *not* on Windows with MinGW (e.g. via [MSYS2](https://www.msys2.org/) as we
> would normally recommend). It may compile on [cygwin](https://www.cygwin.com/)
> or MSYS2 using their MSYS2 shell (untested). It is however perfectly possible
> to display images on Windows via the [minTTY](https://mintty.github.io/)
> terminal (as provided with [MSYS2](https://www.msys2.org/)) when logged onto a
> remote system - `mrpeek` only needs to be installed on the remote server, not
> the local system (as long as the terminal supports sixel graphics).

## Demonstration

![](mrpeek.gif)

#### Using plot functionality with 4D data

![](mrpeek_plot.gif)


## Getting started

First, you'll need a terminal that supports [sixel encoding](https://github.com/MRtrix3/mrpeek/wiki), the format used for rendering images inline. For example:

- [mlterm](https://freshcode.club/projects/mlterm) on Linux or macOS
- [iTerm2](https://www.iterm2.com/) on macOS
- [minTTY](https://mintty.github.io/) on Windows (available by default with
  [MSYS2](https://www.msys2.org/) installs)
- [WSLtty](https://github.com/mintty/wsltty) on [Windows Subsystem for Linux (WSL)](https://docs.microsoft.com/en-us/windows/wsl/)

Next, you need to build `mrpeek` from source, following these instructions:


## Step 1: fetch the _MRtrix3_ source code

`mrpeek` is built an [external module for
MRtrix3](https://mrtrix.readthedocs.io/en/latest/tips_and_tricks/external_modules.html).
If you already have an existing _MRtrix3 source_ installation, feel free to use that
and skip this step.

> **NOTE:** if you've installed _MRtrix3_ using our [binary
> installers](https://www.mrtrix.org/download/), you will still need to follow
> these instructions to fetch the _source_ code and all required dependencies. 

Otherwise, if all you need is to compile `mrpeek`, the following is probably
the simplest solution: it will check out the _MRtrix3_ codebase and configure
it for command-line only use (avoiding the Qt & OpenGL checks), without
generating the _MRtrix3_ shared library (which will make the resulting
executable easier to relocate wherever you need to). 

Note that there is no need to actually build all of _MRtrix3_ if you don't want
to: the next stage will compile what needs to be compiled for `mrpeek` itself.  


> If you're interested in getting a full installation of _MRtrix3_, you can
> follow the [full instructions here](https://mrtrix.readthedocs.io/en/latest/installation/build_from_source.html). 

- Make sure you have all the [required
  dependencies](https://mrtrix.readthedocs.io/en/latest/installation/build_from_source.html#install-dependencies).
  If you're only interested in compiling `mrpeek`, the only required dependencies
  are:
  - a C++11 compliant compiler
  - Python version >= 2.7
  - The zlib compression library

- Fetch the MRtrix3 repo:
  ```
  git clone https://github.com/MRtrix3/mrtrix3.git
  ```

- _[optional]_ if you don't already have the _Eigen3_ libraries:
  ```
  git clone -b 3.3.7 https://gitlab.com/libeigen/eigen.git
  export CFLAGS="-idirafter $(pwd)/eigen"
  ```

- Run the _MRtrix3_ configure script:

  This will check for dependencies and set up the parameters of the compile
  process. 
  ```
  cd mrtrix3/
  ./configure -noshared -nogui
  cd ..
  ```

  > **NOTE:**
  > - If you want a full install of MRtrix3, including the full-blown `mrview`
  >   application, remove the `-nogui` option, and make sure 
  >   [Qt is also installed](https://mrtrix.readthedocs.io/en/latest/installation/build_from_source.html#install-dependencies). 
  >
  > - The `-noshared` option will produce executables without the _MRtrix3_ shared
  >   library. This is not typically recommended, but is appropriate if you're
  >   only interested in compiling `mrpeek` itself.



## Step 2: clone this repo and [build it as a module](https://mrtrix.readthedocs.io/en/latest/tips_and_tricks/external_modules.html):

- Clone & build:
   ```
   git clone https://github.com/MRtrix3/mrpeek.git
   cd mrpeek
   ../mrtrix3/build
   ```

   > **NOTE:** that last step assumes that you've followed the instructions
   > above. If you're using your own previous _MRtrix3_ source installation, you may need
   > to edit the path to the `build` script in the last line above. The only
   > requirement is that you invoke the `build` script of the main _MRtrix3_
   > source folder from within the `mrpeek` module folder (if you do this
   > often, we recommend [setting up a symbolic
   > link](https://mrtrix.readthedocs.io/en/latest/tips_and_tricks/external_modules.html#linking-to-the-mrtrix3-core-c-code-only) to simplify the process). 
   
- Try it out:
   ```
   bin/mrpeek /path/to/image.nii
   ```


- Add it to your `PATH` 

  At this point, you may want to include the `bin/` folder in your `PATH`.

  You can follow your system's standard procedures for this, or use the
  `set_path` convienence script provided within the main _MRtrix3_ source folder 
  ([documented here](https://mrtrix.readthedocs.io/en/latest/tips_and_tricks/external_modules.html#adding-modules-to-path)):
  ```
  ../mrtrix3/set_path
  ```
  Note this will only take effect after starting a fresh terminal.





## Working on the code

- We encourage you to [create your own branches](https://git-scm.com/book/en/v2/Git-Branching-Basic-Branching-and-Merging), and commit _very_ often. Once you've made a coherent set of changes, however small, commit them to your branch. This will help keep track of the evolution of the code, and is great practice anyway.

- Once you have code that more or less does what it's supposed to do, don't hesitate to [push your branch to GitHub](https://help.github.com/en/github/using-git/pushing-commits-to-a-remote-repository) and [create a pull request](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request) for it. 

- We have protected the master branch so that changes (pull requests) require [review before merging](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/reviewing-changes-in-pull-requests). This is probably not required for a small project like this, but it's a great way to ensure your changes are noticed by the other contributors and discussed if needed. It's also how things are likely to be done on any reasonable size project, so it's a good idea to get used to the process. 

- Above all, have fun and experiment with the code! It's the best way to learn.
