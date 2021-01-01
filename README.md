Command line control for Korad KA3005P and possibly others

    $ cc -g -Wall -Wextra -Wpedantic -o koctl koctl.c

    $ ./koctl -h
    ===========
    == koctl ==
    ===========
    usage: ./koctl [-d <dev>] [-u <volts>] [-i <amps>] [-{UIoL} 1|0] [-lqh]
    	-d <device>: UART device (def: /dev/ttyACM0)
    	-u <voltage>: Set target voltage (0.0 - 30.0)
    	-i <current>: Set target current (0.0 - 5.0)
    	-U <1|0>: Enable/disable over-voltage protection
    	-I <1|0>: Enable/disable over-current protection
    	-o <1|0>: Enable/disable output
    	-L <1|0>: Enable/disable front panel lock
    	-l: Log U/I/P to stdout
    	-q: Force one U/I/P output
    	-h: Display brief usage statement and terminate
    
    When no options are given, -q is implied
    
    The output looks like this:
    16094778119	20.32 V	4.001 A	81.30 W
    that is:
    16094778119(tab)20.32 V(tab)4.001 A(tab)81.30 W
    where the first column is a unix timestamp in 10ths
    of a second
    
    (C) 2021, Timo Buhrmester (contact: fstd+koctl@pr0.tips)
    
    $ ./koctl
    16094791927	00.00 V	0.000 A	0.00 W
    $ ./koctl -u7 -i1 -o1
    $ ./koctl
    16094791975	05.11 V	1.005 A	5.14 W
    $ ./koctl -l
    16094792000	05.11 V	1.005 A	5.14 W
    16094792001	05.11 V	1.005 A	5.14 W
    16094792002	05.11 V	1.005 A	5.14 W
    16094792003	05.11 V	1.005 A	5.14 W
    16094792004	05.11 V	1.005 A	5.14 W
    ^C
    $ ./koctl -o0 -q
    16094792080	00.00 V	0.000 A	0.00 W
    $ 
