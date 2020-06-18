# `mrpeek`: a medical image viewer in the terminal

Launched as a project for the [OHBM BrainHack 2020](https://ohbm.github.io/hackathon2020/), `mrpeek` allows to quickly inspect 3-D and 4-D medical images without leaving the terminal. Demo [here](https://twitter.com/jdtournier/status/1273657837034655744?s=20).

## Getting started

First, you'll need a terminal that supports [sixel encoding](https://github.com/MRtrix3/mrpeek/wiki), the format used for rendering images inline. For example:

- mlterm on Linux
- iTerm2 on macOS
- minTTY on Windows

Next, you need to build `mrpeek` from source, following these instructions:

1. [install MRtrix3 from source](https://mrtrix.readthedocs.io/en/latest/installation/build_from_source.html) (you will need `zlib` and [Eigen 3.3](http://eigen.tuxfamily.org/)):

   `mrpeek` is built an [external module for MRtrix3](https://mrtrix.readthedocs.io/en/latest/tips_and_tricks/external_modules.html). If you already have an existing _source_ installation, feel free to use that and skip this step (note: binary installations won't work for this). Otherwise, if all you need is to compile `mrpeek`, the following is probably the simplest solution: it will check out the _MRtrix3_ codebase and configure it for command-line only use (avoiding the Qt & OpenGL checks), without generating the _MRtrix3_ shared library (which will make the resulting executable easier to relocate wherever you need to). 
   ```
   git clone https://github.com/MRtrix3/mrtrix3.git
   cd mrtrix3
   ./configure -noshared -nogui
   ```
   Note that there is no need to actually build all of _MRtrix3_ if you don't want to: the next stage will compile what needs to be compiled for `mrpeek` itself.  

2. clone this repo and [build it as a module](https://mrtrix.readthedocs.io/en/latest/tips_and_tricks/external_modules.html):
   ```
   git clone https://github.com/MRtrix3/mrpeek.git
   cd mrpeek
   ../build
   ```
   
3. try it out:
   ```
   bin/mrpeek /path/to/image.nii
   ```
   At this point, you may want to include the `bin/` folder in your `PATH`. Please follow your system's standard procedures for this.

## Working on the code

- We encourage you to [create your own branches](https://git-scm.com/book/en/v2/Git-Branching-Basic-Branching-and-Merging), and commit _very_ often. Once you've made a coherent set of changes, however small, commit them to your branch. This will help keep track of the evolution of the code, and is great practice anyway.

- Once you have code that more or less does what it's supposed to do, don't hesitate to [push your branch to GitHub](https://help.github.com/en/github/using-git/pushing-commits-to-a-remote-repository) and [create a pull request](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request) for it. 

- We have protected the master branch so that changes (pull requests) require [review before merging](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/reviewing-changes-in-pull-requests). This is probably not required for a small project like this, but it's a great way to ensure your changes are noticed by the other contributors and discussed if needed. It's also how things are likely to be done on any reasonable size project, so it's a good idea to get used to the process. 

- Make sure you communicate with us and most importantly with each other. Don't hesitate to discuss on the Mattermost channel, document issues / features required on the [issue tracker](https://github.com/MRtrix3/mrpeek/issues) and/or [project board](https://github.com/MRtrix3/mrpeek/projects/1), and engage via the video channel. Collaborative projects require a _lot_ more discussion and coordination than you might expect!

- Above all, have fun and experiment with the code! It's the best way to learn.
