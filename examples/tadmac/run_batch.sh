#!/bin/bash
if [ -d "results/5" ]; then
	rm -rf results/5/*.sca
fi

../miximexamples -r 0 -n ..:../../src/base:../../src/inet_stub:../../src/modules --tkenv-image-path=../../images -l ../../src/mixim -u Cmdenv omnetpp.ini -x test -g -r 1

# for i in {0..1}
# do
# 	if [ $i -lt 10 ]; then
# 		../miximexamples -r 0 -n ..:../../src/base:../../src/inet_stub:../../src/modules --tkenv-image-path=../../images -l ../../src/mixim --output-scalar-file=results/5/0$i.sca -u Cmdenv omnetpp.ini -x test -g
# 	else
# 		../miximexamples -r 0 -n ..:../../src/base:../../src/inet_stub:../../src/modules --tkenv-image-path=../../images -l ../../src/mixim --output-scalar-file=results/5/$i.sca -u Cmdenv omnetpp.ini -x test -g
# 	fi
# done