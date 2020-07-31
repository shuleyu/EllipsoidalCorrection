# Download, Compile, Run

### Download:

```
$ git clone --recursive https://github.com/shuleyu/EllipsoidalCorrection.git
```

If `--recursive` is not added, the dependencies will not be downloaded. In such case, do: 

```
$ cd ./CPP-Library-Examples
$ git submodule update --init --recursive
```


### Compile: modify Makefile (usually not required) then compile.

```
$ vim Makefile
$ make
```

### Run: modify "inputFile\*.txt" and run.

```
./EllipsoidalCorrection.out inputFileBenchmark.txt
```
