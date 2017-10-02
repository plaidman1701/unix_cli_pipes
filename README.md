This is a unix CLI emulator that handles multiple pipes. Each piped segment is spun off as a separate POSIX child thread and piped to the next segment.

Each pipe character requires a space before and afterwards. Input and output redirects can be used in the first and last segment respectively, as long as there are no spaces between the redirection operator and the file name. For example, the following will work;

```
> cat <input.txt | tr a A | tr b B >output.txt
```

However,

```
> cat < input.txt|tr a A|tr b B > output.txt
```

Is all kinds of wrong
