# Minecraft Dura Mapper

WORK IN PROGRESS

A simple minecraft mapping utility for generating overhead view maps from a minecraft world.

We're designing it to be simple, relying on few dependencies, and resistant to version updates. (We'll see how that goes)

Currently the mapper only reads mca region files and outputs them to a directory.



# Use
Currently to use the mapper you need to build from source and so far we've only worked on linux. You can build with the python script like so:

```
python3 bear_make.py make
```
We're currently using python Python 3.12.3

This should make you an executable which you can run like so:
```
./minecraft_dura_mapper
```

We'll probably do builds for windows and other OS in the future. (Right now the mapper is incomplete still)



