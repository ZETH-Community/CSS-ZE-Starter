# async_connect

You need to compile https://github.com/libuv/libuv for 32bit like below if you have a 64bit OS.
```
sh autgen.sh
./configure --build=i686-pc-linux-gnu "CFLAGS=-m32" "CXXFLAGS=-m32" "LDFLAGS=-m32" --disable-shared --enable-static
make
```

Put the `libuv/include` folder and `libuv/.libs/libuv.a` in `extensions/libuv`.
