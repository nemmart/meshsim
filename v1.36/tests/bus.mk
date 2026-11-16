# bus.mk: the rule (P2, inventory C2): suite_bus, v1.3's BusTest on the framework's port kinds (tests/suite_bus.cpp).
# Its throws checks are the framework's debug assertions, so the runner's bus step and make check run the debug build.
$(B)/suite_bus: $(B)/tests/suite_bus.o $(B)/tests/suite.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
