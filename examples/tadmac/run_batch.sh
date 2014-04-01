#!/bin/bash
for i in {0..99}
do
	if [ $i -lt 10 ]; then
		../miximexamples -r 0 -n ..:../../src/base:../../src/inet_stub:../../src/modules --tkenv-image-path=../../images -l ../../src/mixim --output-scalar-file=results/4/0$i.sca -u Cmdenv omnetpp.ini
	else
		../miximexamples -r 0 -n ..:../../src/base:../../src/inet_stub:../../src/modules --tkenv-image-path=../../images -l ../../src/mixim --output-scalar-file=results/4/$i.sca -u Cmdenv omnetpp.ini
	fi
done