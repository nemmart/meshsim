# tools/delivery (WORKER19, P1, D42): v1.3's ConvertDelivery and SyntheticDelivery in C++ (the input pipeline's and the
# acceptance kit's; pipeline/inputs/ calls convert_delivery). Built in every build; checked against v1.3's on the
# oracle side (oracle/convert_crosscheck.sh, oracle/synthetic_crosscheck.sh).
#   build/<cc>-<mode>/convert_delivery SRC DST PROGRAM [--manifest M]
$(B)/convert_delivery: $(B)/tools/delivery/convert_delivery.o $(B)/tests/sha256.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/convert_delivery
