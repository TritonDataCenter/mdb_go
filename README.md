## Usage

```
> ::load /path/to/go.so
> ::gostack -p name
main.sub5()
main.sub4()
main.sub3()
main.sub2()
main.sub1()
main.main()
runtime.main()
runtime.goexit()
> ::walk goframe
0xfffffd7ffef9fea0
0xfffffd7ffef9fed0
0xfffffd7ffef9fef8
0xfffffd7ffef9ff18
0xfffffd7ffef9ff30
0xfffffd7ffef9ff40
0xfffffd7ffef9ff98
0xfffffd7ffef9ffa8
> ::walk goframe | ::goframe
fffffd7fffdfde10 = {
        entry = 400c20,
        nameoff = 2168 (name = main.sub4),
        args = 20,
        frame = 30,
        pcsp = 2173,
        pcfile = 217a,
        pcln = 217d,
        npcdata = 0,
        nfuncdata = 2,
}
fffffd7fffdfde10 = {
        entry = 400c80,
        nameoff = 21c0 (name = main.sub3),
        args = 18,
        frame = 28,
        pcsp = 21cb,
        pcfile = 21d2,
        pcln = 21d5,
        npcdata = 0,
        nfuncdata = 2,
}
...
```
