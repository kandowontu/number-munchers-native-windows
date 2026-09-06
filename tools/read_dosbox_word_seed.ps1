param(
    [Parameter(Mandatory)]
    [int]$ProcessId
)

$ErrorActionPreference = 'Stop'
$workspace = [System.IO.Path]::GetFullPath(
    (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$expectedExecutable = [System.IO.Path]::GetFullPath(
    (Join-Path $workspace 'tools\_third_party\dosbox-x\mingw-build\mingw-sdl2\dosbox-x.exe'))
$process = Get-Process -Id $ProcessId -ErrorAction Stop
if ([System.IO.Path]::GetFullPath($process.Path) -ne $expectedExecutable) {
    throw "PID $ProcessId is not the workspace DOSBox-X executable: $($process.Path)"
}
$process.Refresh()
if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
    throw "Refusing to inspect DOSBox-X with a visible/focusable window."
}

Add-Type @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Linq;

public static class DosboxWordMemoryReader {
    [StructLayout(LayoutKind.Sequential)]
    private struct MEMORY_BASIC_INFORMATION {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public UIntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, int processId);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ReadProcessMemory(
        IntPtr process, IntPtr address, byte[] buffer, UIntPtr size,
        out UIntPtr bytesRead);
    [DllImport("kernel32.dll")]
    private static extern UIntPtr VirtualQueryEx(
        IntPtr process, IntPtr address, out MEMORY_BASIC_INFORMATION info,
        UIntPtr length);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);

    private const uint PROCESS_QUERY_INFORMATION = 0x0400;
    private const uint PROCESS_VM_READ = 0x0010;
    private const uint MEM_COMMIT = 0x1000;
    private const uint PAGE_GUARD = 0x100;
    private const uint PAGE_NOACCESS = 0x01;

    private static int Find(byte[] haystack, int count, byte[] needle) {
        for (int start = 0; start + needle.Length <= count; ++start) {
            int index = 0;
            while (index < needle.Length && haystack[start + index] == needle[index]) {
                ++index;
            }
            if (index == needle.Length) return start;
        }
        return -1;
    }

    public static long[] FindAll(int processId, byte[] signature) {
        IntPtr process = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, processId);
        if (process == IntPtr.Zero) {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        var matches = new List<long>();
        try {
            long address = 0;
            int infoSize = Marshal.SizeOf<MEMORY_BASIC_INFORMATION>();
            const int chunkBytes = 4 * 1024 * 1024;
            while (address >= 0 && address < 0x00007fffffffffffL) {
                MEMORY_BASIC_INFORMATION info;
                UIntPtr queried = VirtualQueryEx(
                    process, new IntPtr(address), out info, new UIntPtr((uint)infoSize));
                if (queried == UIntPtr.Zero) break;
                long baseAddress = info.BaseAddress.ToInt64();
                long regionBytes = checked((long)info.RegionSize.ToUInt64());
                bool readable = info.State == MEM_COMMIT &&
                    (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
                if (readable && regionBytes > 0 && regionBytes <= int.MaxValue) {
                    long offset = 0;
                    int overlap = Math.Max(0, signature.Length - 1);
                    byte[] buffer = new byte[Math.Min(chunkBytes + overlap, (int)regionBytes)];
                    while (offset < regionBytes) {
                        int request = (int)Math.Min(buffer.Length, regionBytes - offset);
                        UIntPtr read;
                        if (ReadProcessMemory(process, new IntPtr(baseAddress + offset),
                                              buffer, new UIntPtr((uint)request), out read)) {
                            int count = checked((int)read.ToUInt64());
                            int search = 0;
                            while (search < count) {
                                int found = Find(new ArraySegment<byte>(buffer, search,
                                    count - search).ToArray(), count - search, signature);
                                if (found < 0) break;
                                matches.Add(baseAddress + offset + search + found);
                                search += found + 1;
                            }
                        }
                        if (request <= overlap) break;
                        offset += request - overlap;
                    }
                }
                long next = baseAddress + regionBytes;
                if (next <= address) break;
                address = next;
            }
        } finally {
            CloseHandle(process);
        }
        return matches.ToArray();
    }

    public static byte[] Read(int processId, long address, int count) {
        IntPtr process = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, processId);
        if (process == IntPtr.Zero) {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        try {
            byte[] result = new byte[count];
            UIntPtr read;
            if (!ReadProcessMemory(process, new IntPtr(address), result,
                                   new UIntPtr((uint)count), out read) ||
                read.ToUInt64() != (ulong)count) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return result;
        } finally {
            CloseHandle(process);
        }
    }
}
'@

# The executable startup relocates DS to load-segment + 0x22c8.  Borland's
# four-byte rand state is DS:532e, therefore load-image offset 0x27fae.  The
# following immutable table begins four bytes later and is unique in the
# unpacked image; using it avoids searching for the mutable state itself.
$image = [System.IO.File]::ReadAllBytes(
    (Join-Path $workspace 'analysis\WM-unpacked-image.bin'))
$stateImageOffset = 0x27fae
$signatureImageOffset = $stateImageOffset + 4
$signatureLength = 40
$signature = [byte[]]::new($signatureLength)
[Array]::Copy($image, $signatureImageOffset, $signature, 0, $signatureLength)
$matches = [DosboxWordMemoryReader]::FindAll($ProcessId, $signature)

$results = foreach ($match in $matches) {
    $imageBase = $match - $signatureImageOffset
    $stateBytes = [DosboxWordMemoryReader]::Read(
        $ProcessId, $imageBase + $stateImageOffset, 4)
    [pscustomobject]@{
        ProcessId = $ProcessId
        ImageBase = ('0x{0:X}' -f $imageBase)
        State = ('0x{0:X8}' -f [BitConverter]::ToUInt32($stateBytes, 0))
        LowSeed = [BitConverter]::ToUInt16($stateBytes, 0)
        HighWord = [BitConverter]::ToUInt16($stateBytes, 2)
    }
}

if (@($results).Count -ne 1) {
    $results | Format-Table -AutoSize
    throw "Expected one live WM load-image match, found $(@($results).Count)."
}
$results
