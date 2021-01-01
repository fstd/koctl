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
