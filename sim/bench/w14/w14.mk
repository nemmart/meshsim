# w14.mk: WORKER14's benchmark (bench/w14/), included by sim/Makefile.
#   tracer_play  the Tracer's speed alone on a real run's calls (convert a TracerConf log, then run it timed; timing: clang
#                release, D18). ref's side: bench/w14/TracerPlay.java.
$(B)/tracer_play: $(B)/bench/w14/tracer_play.o $(TRACER_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
