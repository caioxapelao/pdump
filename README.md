# pdump
**pdump** is an application that:
1. Prepares syscall stubs for ntdll functions
2. Opens a process
3. Gets its loaded modules
4. Dumps each one out of memory into a file.
Output files are ready for inspection in tools such as **IDA Pro** and **Ghidra**.

# How to use
`pdump.exe <processname>`


The command above will dump the modules loaded in the process with the specified name into the current directory.