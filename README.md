# small-linux-shell
Implementation of a subset of shell features written in C. Program functionality includes the following:
1. Command prompt that supports a maximum length of 2048 characters and 512 arguments
2. Comments that begin with character `#`
3. Expansion of the variable `$$` into the process ID of the small shell
4. Input and output direction using the characters `<` and `>`, respectively
5. Execution of commands in both foreground and background (commands with an `&` at the end will be run in the background)
6. SIGINT (CTRL+C) terminates all foreground processes
7. SIGTSTP (CTRL+Z) toggles background execution capability

### Example Execution
##### Compilation:

```
$ gcc --std=gnu99 -o smallsh smallsh.c
```


##### Execution:

```
$ ./smallsh
```


##### Command line syntax:

```
: command [arg1 arg2 ... ] [ < input_file ] [ < output_file ] [&]
```
