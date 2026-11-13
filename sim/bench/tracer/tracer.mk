# tracer.mk: the benchmark (bench/tracer/), included by sim/Makefile.
#   tracer_play  the Tracer's speed alone on a real run's calls (convert a TracerConf log, then run it timed; timing: clang
#                release, D18). ref's side: oracle/bench/TracerPlay.java.
$(B)/tracer_play: $(B)/bench/tracer/tracer_play.o $(TRACER_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
