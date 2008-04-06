LEVEL = ../..
DIRS := lib Driver 

include $(LEVEL)/Makefile.common

test::
	@ $(MAKE) -C test 

report::
	@ $(MAKE) -C test report

clean::
	@ $(MAKE) -C test clean

.PHONY: test report clean
