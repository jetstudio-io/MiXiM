#!/bin/bash
#rm -rf results/7/*.sca
for i in {0..100}
do
	if [ $i -lt 10 ]; then
		../miximexamples -r 0 -n ..:../../src/base:../../src/inet_stub:../../src/modules --tkenv-image-path=../../images -l ../../src/mixim --output-scalar-file=results/7/0$i.sca -u Cmdenv omnetpp.ini
	else
		../miximexamples -r 0 -n ..:../../src/base:../../src/inet_stub:../../src/modules --tkenv-image-path=../../images -l ../../src/mixim --output-scalar-file=results/7/$i.sca -u Cmdenv omnetpp.ini
	fi
done