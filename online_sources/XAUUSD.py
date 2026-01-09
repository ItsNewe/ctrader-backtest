#!/usr/bin/env python
# -*- coding: utf-8 -*-


f_in = open("C:\Users\user\PycharmProjects\XAUUSD\XAUUSDH2.csv", "r")
f_out = open("C:\Users\user\PycharmProjects\XAUUSD\XAUUSDH2_mod.csv", "w")
for line in f_in:
    date, open, high, low, close, volume, rest  = line.split(",")
    counter = 0
    for i in open:
        if (divmod(counter,2)[1]) == 1:
            f_out.write(open[counter])
        counter = counter + 1
    f_out.write(",")
    counter = 0
    for i in high:
        if (divmod(counter,2)[1]) == 1:
            f_out.write(high[counter])
        counter = counter + 1
    f_out.write(",")
    counter = 0
    for i in low:
        if (divmod(counter,2)[1]) == 1:
            f_out.write(low[counter])
        counter = counter + 1
    f_out.write(",")
    counter = 0
    for i in close:
        if (divmod(counter,2)[1]) == 1:
            f_out.write(close[counter])
        counter = counter + 1
    f_out.write(",")
    counter = 0
    for i in volume:
        if (divmod(counter,2)[1]) == 1:
            f_out.write(volume[counter])
        counter = counter + 1
    f_out.write('\n')

    # if line.startswith( "[('adc:1:polyctl_" ):
    #
    #     for k, v in reg.items():
    #         if k in line:
    #             address = v
    #
    #             line2 = line.replace("[('adc:1:", '')
    #             if ":" in line2:
    #                 a, b = line2.split(":")
    #                 c, d = b.split("'")
    #                 address = address + int(c)
    #
    #             if line.endswith("None)]\n"):
    #                 print("{} {}".format("REG_GET =", format(address, '#010x')))
    #                 f_out.write("{} {}\n".format(";", k))
    #                 f_out.write("{} {}\n".format("REG_GET =", format(address, '#010x')))
    #             else:
    #                 e,f = line.split(",")
    #                 value = f.replace(')]', '')
    #                 print("{} {} {}".format("REG_SET =", format(address, '#010x'), format(int(value), '#010x')))
    #                 f_out.write("{} {}\n".format(";", k))
    #                 f_out.write("{} {} {}\n".format("REG_SET =", format(address, '#010x'), format(int(value), '#010x')))

f_out.close()
f_in.close()