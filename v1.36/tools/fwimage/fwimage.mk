# tools/fwimage: fwimage makes the data a chip run's firmware is built around: the sort runs' key files and the master
# tile's run images, in the RTL's formats (fwimage.cpp's header; fw/README.md). fw/check_images.sh and fw/build.sh
# check its output against the committed keys and the committed masters' images; run_tests.sh builds the 64K key file
# with it.
#   build/<cc>-<mode>/fwimage keys RECORDS OUT; ... run sort kv32|k128 RECORDS TILE.elf OUT; ... check-run MASTER.elf RUN.bin
$(B)/fwimage: $(B)/tools/fwimage/fwimage.o
	$(CXX) $(FLAGS) $^ -o $@
all: $(B)/fwimage
