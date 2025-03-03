/** 
 * Copyright (C) 2024 Niels Martin Hansen
 * 
 * This file is part of the Emulated SCSI Toolbox
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include <sys/types.h> 
#include <sys/stat.h> 
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <ctype.h>
#include <wcvector.h>

#include "../include/aspi.h"
#include "../include/scsidefs.h"
#include "../include/toolbox.h"
#include "../include/estb.h"


static char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static bool AskForConfirmation(const char *question)
{
    fprintf(stderr, "%s (Y/N) ", question);
    while(1) {
        int response = getchar();
        switch (response) {
            case 'y':
            case 'Y':
                return true;
            case 'n':
            case 'N':
                return false;
            default:
                continue;
        }
    }
}


struct FoundToolboxDevice {
    unsigned char adapter_id;
    char name;
    ToolboxDeviceList tdl;

    FoundToolboxDevice() {
        adapter_id = 0xFF;
        name = ' ';
        memset(tdl.device_type, TOOLBOX_DEVTYPE_NONE, sizeof(tdl.device_type));
    }

    bool operator== (const FoundToolboxDevice &other) const {
        return adapter_id == other.adapter_id && name == other.name;
    }
};

static int DoDeviceInfo(int argc, const char *argv[])
{
    int r = InitSCSI();
    int dev_id;
    int errors = 0;
    DeviceInquiryResult di;
    WCValOrderedVector<FoundToolboxDevice> tbdevs;

    (void)argc; // unused parameter
    (void)argv; // unused parameter

    if (r) return r;

    //tbdevs.reserve(_devices.size());

    printf(
        "Addr   Vendor   Model            Type       Adapter            Emulation Dev\n"
        "----------------------------------------------------------------------------\n"
    );

    for (dev_id = 0; dev_id < _devices.entries(); dev_id++) {
        // Query the device details
        Device &dev = _devices[dev_id];
        r = DeviceInquiry(dev, &di);
        if (r) {
            errors++;
        }

        // Check for toolbox support status
        FoundToolboxDevice *tbdev = NULL;
        for (int tbdevid = 0; tbdevid < tbdevs.entries(); tbdevid++) {
            if (tbdevs[tbdevid].adapter_id != dev.adapter_id) continue;
            if (tbdevs[tbdevid].tdl.device_type[dev.target_id] == TOOLBOX_DEVTYPE_NONE) continue;
            tbdev = &tbdevs[tbdevid];
            break;
        }
        if (tbdev == NULL) {
            FoundToolboxDevice newtbdev;
            bool has_toolbox = ToolboxListDevices(dev, newtbdev.tdl);
            if (has_toolbox) {
                newtbdev.adapter_id = dev.adapter_id;
                newtbdev.name = LETTERS[tbdevs.entries()];
                tbdevs.append(newtbdev);
                tbdev = &tbdevs.last();
            }
        }

        // Print out the device details
        printf("%-6s %-8s %-16s %-10s %-18s %-10s %c \n",
            dev.name,
            di.vendor,
            di.product,
            GetDeviceTypeName(dev.devtype),
            _adapters[dev.adapter_id].adapter_id,
            tbdev ? GetToolboxDeviceTypeName(tbdev->tdl.device_type[dev.target_id]) : "",
            tbdev ? tbdev->name : ' '
            );
    }
    
    return 0;
}


static void PrintFileList(const WCValOrderedVector<ToolboxFileEntry> &files)
{
    printf("%d files found\n", files.entries());

    for (size_t i = 0; i < files.entries(); i++) {
        const ToolboxFileEntry &tfe = files[i];
        unsigned long filesize = tfe.GetSize();
        char sizestr[20];
        if (filesize < 1000) {
            snprintf(sizestr, sizeof(sizestr), "%ld B", filesize);
        } else if (filesize < 1000000) {
            snprintf(sizestr, sizeof(sizestr), "%ld,%03ld B", filesize / 1000, filesize % 1000);
        } else if (filesize < 1000000000) {
            unsigned long M = filesize / 1000000;
            unsigned long r = filesize % 1000000;
            snprintf(sizestr, sizeof(sizestr), "%ld,%03ld,%03ld B", M, r / 1000, r % 1000);
        } else {
            unsigned long M = filesize / 1000000;
            unsigned long r = filesize % 1000000;
            snprintf(sizestr, sizeof(sizestr), "%ld,%03ld,%03ld,%03ld B", M / 1000, M % 1000, r / 1000, r % 1000);
        }
        
        printf("%d %s%-32s %16s\n", tfe.index, tfe.type ? " " : "/", tfe.name, sizestr);
    }
}


static int DoListImages(int argc, const char *argv[])
{
    int r = InitSCSI();

    (void)argc; // unused parameter

    if (r) return r;

    const Device *dev = GetDeviceByName(argv[0]);
    if (!dev) {
        fprintf(stderr, "Device ID not found: %s\n", argv[0]);
        return 16;
    }
    
    printf("Retrieving images from device %s type %d (%s)...\n",
        dev->name, dev->devtype, GetDeviceTypeName(dev->devtype));

    WCValOrderedVector<ToolboxFileEntry> images;
    
    if (ToolboxGetImageList(*dev, images)) {
        PrintFileList(images);
        return 0;
    } else {
        return 1;
    }
}


static int DoSetImage(int argc, const char *argv[])
{
    int r = InitSCSI();
    int newimage = -1;

    (void)argc; // unused parameter

    if (r) return r;

    const Device *dev = GetDeviceByName(argv[0]);
    if (!dev) {
        fprintf(stderr, "Device ID not found: %s\n", argv[0]);
        return 16;
    }

    if (sscanf(argv[1], "%d", &newimage) != 1 || newimage < 0 || newimage > 100) {
        fprintf(stderr, "Illegal image index, please use an index returned from the 'lsimg' command.\n");
        return 17;
    }
    
    printf("Set loaded image for device %s type %d (%s)\n", dev->name, dev->devtype, GetDeviceTypeName(dev->devtype));

    r = ToolboxSetImage(*dev, newimage);
    if (r == 1) printf("Set next image command sent successfully.\n");

    return r != 0;
}

struct SetNameMatch
{
    char* name;
    int index;
};

static void cleanupMatches(SetNameMatch* matches, int matchCount)
{
    for (int n = 0; n < matchCount; ++n) {
        free(matches[n].name);
    }
}

static size_t mystrnlen(const char *s, size_t maxlen) {
    size_t i;
    for (i = 0; i < maxlen; ++i) {
        if (s[i] == '\0') {
            return i;
        }
    }
    return maxlen;
}

static char* mystrndup(const char* s, size_t n)
{
    char *new_string;
    size_t len = mystrnlen(s, n);

    new_string = (char *)malloc(len + 1);
    if (new_string == NULL) {
        return NULL;
    }
    memcpy(new_string, s, len);
    new_string[len] = '\0';
    return new_string;
}

static bool IsSetNameMatch(const char* argv1, char* candidate)
{
    // Loop through each position in the main string
    for (int i = 0; candidate[i] != '\0'; ++i) {
        int j = 0;
        // Check for substring match starting at each position
        while (candidate[i + j] != '\0' && argv1[j] != '\0' &&
               tolower(candidate[i + j]) == tolower(argv1[j])) {
            ++j;
        }
        // If the whole substring matched
        if (argv1[j] == '\0') {
            return true; // Found the substring
        }
    }
    return false;
}

static int DoSetName(int argc, const char *argv[])
{
    int rr = InitSCSI();

    (void)argc; // unused parameter

    if (rr) return rr;

    const Device *dev = GetDeviceByName(argv[0]);
    if (!dev) {
        fprintf(stderr, "Device ID not found: %s\n", argv[0]);
        return 16;
    }

    const char* fn = getenv("SCSITB_FILES");
    if (fn == NULL) {
        fprintf(stderr, "SCSITB_FILES environment variable not set");
        return 19;
    }

    char* argv1 = strdup(argv[1]);
    // modifiers are [index], at the end
    int mod = -1;
    char* mods = strchr(argv1, '[');
    if (mods != NULL) {
        const char* mode = strchr(mods + 1, ']');
        if (mode == NULL) {
            fprintf(stderr, "Started a modifier but no end");
            return 20;
        }

        if (sscanf(mods, "[%d]", &mod) != 1 || mod < 0 || mod > 100) {
            fprintf(stderr, "Modifier bad or out of range");
            return 21;
        }

        // remove the modifier
        *mods = '\0';
    }

    // read file, look at lines
    // first line is index 1, second line is index 2, etc.
    char buf[512];
    size_t off = 0;
    size_t size = 0;
    FILE* f = fopen(fn, "r");
    if (f == NULL) {
        fprintf(stderr, "Can't open SCSITB_FILES filename (%s)", fn);
        return 22;
    }
    SetNameMatch matches[10];
    int matchCount = 0;
    int index = 0;
    while (!feof(f)) {
        size_t rd = fread(buf + off, 1, sizeof(buf) - off, f);
        if (rd > 0) {
            size = off + rd;
            // find \n, check if line matches query
            int suboff = 0;
            for (;;) {
                // printf("Looking for \\n %d %d\n", suboff, size - suboff);
                char* nl = reinterpret_cast<char*>(memchr(buf + suboff, '\n', size - suboff));
                if (nl == NULL) {
                    // memmove and break
                    if (suboff > 0) {
                        memmove(buf, buf + suboff, size - suboff);
                        off = size = size - suboff;
                    } else {
                        // this would be bad
                        fprintf(stderr, "No '\n' in the entire SCSITB_FILES file?");
                        cleanupMatches(matches, matchCount);
                        fclose(f);
                        return 23;
                    }
                    break;
                } else if (matchCount < 10) {
                    int subend = nl - buf;
                    int trim = 0;
                    while (subend - trim >= 0 && (buf[subend - trim] == '\n' || buf[subend - trim] == '\r')) {
                        ++trim;
                    }
                    char* candidate = mystrndup(buf + suboff, subend - suboff - trim + 1);
                    // printf("testing... %d '%s' '%s'\n", mod, argv1, candidate);
                    if (IsSetNameMatch(argv1, candidate)) {
                        // printf("found\n");
                        matches[matchCount].name = candidate;
                        matches[matchCount].index = index;
                        ++matchCount;
                    } else {
                        // printf("no\n");
                        free(candidate);
                    }
                    suboff = subend + 1;

                    ++index;
                } else {
                    fprintf(stderr, "More than 10 matches");
                    cleanupMatches(matches, matchCount);
                    fclose(f);
                    return 24;
                }
            }
        } else {
            fprintf(stderr, "Unable to read from SCSITB_FILES %d %d", errno, off);
            cleanupMatches(matches, matchCount);
            fclose(f);
            return 25;
        }
    }
    fclose(f);

    if (matchCount == 0) {
        fprintf(stderr, "No matches");
        cleanupMatches(matches, matchCount);
        return 26;
    } else if (matchCount == 1 || mod >= 0) {
        if (mod >= matchCount) {
            fprintf(stderr, "Modifier out of range %d vs a count of %d\n", mod, matchCount);
            cleanupMatches(matches, matchCount);
            return 27;
        }
        int newimage = matches[mod >= 0 ? mod : 0].index;
        printf("Set loaded image for device %s type %d (%s) to index %d\n", dev->name, dev->devtype, GetDeviceTypeName(dev->devtype), newimage);
        rr = ToolboxSetImage(*dev, newimage);
        if (rr == 1) printf("Set next image command sent successfully.\n");
    } else {
        printf("Multiple candidates for setname:\n");
        for (int n = 0; n < matchCount; ++n) {
            printf("  %d: %s (%d)\n", n, matches[n].name, matches[n].index);
        }
        rr = 1;
    }
    cleanupMatches(matches, matchCount);

    free(argv1);

    return rr == 1 ? 0 : rr;
}

static int DoListSharedDir(int argc, const char *argv[])
{
    int r = InitSCSI();

    (void)argc; // unused parameter

    if (r) return r;

    const Device *dev = GetDeviceByName(argv[0]);
    if (!dev) {
        fprintf(stderr, "Device ID not found: %s\n", argv[0]);
        return 16;
    }
    
    printf("Retrieving file list from device %s type %d (%s)...\n",
        dev->name, dev->devtype, GetDeviceTypeName(dev->devtype));

    WCValOrderedVector<ToolboxFileEntry> files;
    
    if (ToolboxGetSharedDirList(*dev, files)) {
        PrintFileList(files);
        return 0;
    } else {
        return 1;
    }
}


static void CleanFileName(char *dstfn, const char *srcfn, int srclen)
{
    int tmplen = strlen(srcfn);
    if (tmplen < srclen) srclen = tmplen;

    int baselen = 0;
    char *extstart = NULL;
    int extlen = 0;
    for (; *srcfn != '\0'; ++srcfn) {
        char c = *srcfn;
        if (srclen-- <= 0) break;
        // Check for various illegal characters and skip them
        if (c < ' ') continue;
        if (c == '/') continue;
        if (c == '\\') continue;
        if (c == '<') continue;
        if (c == '>') continue;
        if (c == '|') continue;
        if (c == '*') continue;
        if (c == '?') continue;
        if (c == ' ') continue;
        // Check for period to start the extension
        if (c == '.') {
            // Except if there is no base filename yet
            if (baselen == 0) continue;
            // If there is no extension yet, capture the extension start location
            if (extstart == NULL) extstart = dstfn;
            // Otherwise reset to the extension start location
            else dstfn = extstart;
            // Extension is now 0 characters
            extlen = 0;
            *dstfn = '\0';
        }
        // Now write the character, if there is room
        if (extstart == NULL && baselen < 8) {
            *dstfn++ = c;
            baselen++;
        } else if (extstart != NULL && extlen < 4) {
            *dstfn++ = c;
            extlen++;
        }
    }
    // Ensure terminator
    *dstfn = '\0';
    // TODO: check for device name clashes? like CON, PRN, LPTx, COMx, AUX, NUL
}

static int DoGetSharedDirFile(int argc, const char *argv[])
{
    char outfn[128] = "";
    int fileindex = -1;
    int r = InitSCSI();

    if (r) return r;

    const Device *dev = GetDeviceByName(argv[0]);
    if (!dev) {
        fprintf(stderr, "Device ID not found: %s\n", argv[0]);
        return 16;
    }

    if (sscanf(argv[1], "%d", &fileindex) != 1 || fileindex < 0 || fileindex > 100) {
        fprintf(stderr, "Illegal file index, please use an index returned from the 'lsdir' command.\n");
        return 17;
    }
    
    if (argc >= 3) {
        strncpy(outfn, argv[2], sizeof(outfn));
        printf("specified output filename: %s\n", outfn);
    }

    printf("Retrieving file list from device %s type %d (%s)...\n",
        dev->name, dev->devtype, GetDeviceTypeName(dev->devtype));

    WCValOrderedVector<ToolboxFileEntry> files;
    if (!ToolboxGetSharedDirList(*dev, files)) {
        return 1;
    }

    const ToolboxFileEntry *tfe = NULL;
    for (int i = 0; i < files.entries(); i++) {
        if (files[i].index == fileindex) {
            tfe = &files[i];
            break;
        }
    }
    if (tfe == NULL) {
        fprintf(stderr, "Illegal file index, please use an index returned from the 'lsdir' command.\n");
        return 17;
    }

    if (outfn[0] == '\0') {
        CleanFileName(outfn, tfe->name, sizeof(tfe->name));
    }

    // Check if the destination file already exists
    printf("Output file: %s\n", outfn);
    FILE *outfile = fopen(outfn, "rb");
    if (outfile != NULL) {
        fclose(outfile);
        outfile = NULL;
        if (!AskForConfirmation("The output file already exists. Overwrite it?")) {
            fprintf(stderr, "Not overwriting file, aborting.\n");
            return 4;
        }
    }

    // Actually open for writing
    outfile = fopen(outfn, "wb");
    if (outfile == NULL) {
        fprintf(stderr, "Could not open output file for writing\n");
        return 2;
    }

    // TODO: check if destination volume has enough space for transfer first?

    unsigned long totalblocks = (tfe->GetSize() + 4095) / 4096;
    unsigned char *databuf = new unsigned char[4096];
    unsigned long totaltransferred = 0;
    for (unsigned long block = 0; block < totalblocks; block++) {
        printf("  Block %ld / %ld (%d%%)...\r", block+1, totalblocks, (block + 1) * 100 / totalblocks);
        int r = ToolboxGetFileBlock(*dev, fileindex, block, databuf);
        bool error = r < 0;
        if (r == 0) {
            fprintf(stderr,
                "Unexpected zero byte transfer. Maybe your SCSI adapter or ASPI driver does not\n"
                "support the Residual Byte Count Reporting feature?\n"
                );
        }
        if (!error) {
            error = fwrite(databuf, r, 1, outfile) != 1;
            if (error) {
                fprintf(stderr, "Error writing to output file.                  \n");
            } else {
                totaltransferred += r;
            }
        }
        if (error) {
            fprintf(stderr, "Aborting transfer...                   \n");
            ToolboxGetFileBlock(*dev, fileindex, totalblocks-1, databuf);
            delete[] databuf;
            return 3;
        }
    }

    delete[] databuf;
    fclose(outfile);
    return 0;
}

static int DoPutSharedDirFile(int argc, const char *argv[])
{
    int r = InitSCSI();

    (void)argc; // unused parameter

    if (r) return r;

    const Device *dev = GetDeviceByName(argv[0]);
    if (!dev) {
        fprintf(stderr, "Device ID not found: %s\n", argv[0]);
        return 16;
    }

    printf("Verifying destination device %s type %d (%s)...\n",
        dev->name, dev->devtype, GetDeviceTypeName(dev->devtype));

    WCValOrderedVector<ToolboxFileEntry> files;
    if (!ToolboxGetSharedDirList(*dev, files)) {
        return 17;
    }

    const char *inpfn = argv[1];
    const char *outfn = basename(strdup(inpfn)); // assume DOS will clean up the memory on exit

    int infile = _open(inpfn, O_RDONLY | O_BINARY);
    if (infile == -1) {
        fprintf(stderr, "The source file could not be opened for reading.\n");
        return 1;
    }
    long filesize = _filelength(infile);

    for (int i = 0; i < files.entries(); i++)  {
        if (stricmp(outfn, files[i].name) == 0) {
            fprintf(stderr, "Destination filename: %s\n", outfn);
            if (!AskForConfirmation("The destination already contains a file with this name. Overwrite?")) {
                _close(infile);
                return 2;
            }
        }
    }

    if (!ToolboxSendFileBegin(*dev, outfn)) {
        _close(infile);
        return 18;
    }

    const unsigned short BUFSIZE = 512;
    char *buf = new char[BUFSIZE];
    unsigned long block_index = 0;
    unsigned long num_blocks = ((unsigned long)filesize + (BUFSIZE - 1)) / BUFSIZE;
    int error_status = 0;

    printf("Sending: %s => %s\n", inpfn, outfn);

    while (block_index < num_blocks) {
        short data_size = _read(infile, buf, BUFSIZE);
        if (data_size > 0) {
            if (!ToolboxSendFileBlock(*dev, data_size, block_index, buf)) {
                error_status = 18;
                break;
            }
        } else if (data_size < 0) {
            fprintf(stderr, "Error reading file, aborting transfer.\n");
            error_status = 3;
            break;
        }
        printf("  Block %lu / %lu (%d%%)...\r", block_index+1, num_blocks, (block_index + 1) * 100 / num_blocks);
        block_index++;
        if (data_size < BUFSIZE) break;
    }
    printf("  Finished sending %lu blocks            \n", num_blocks);
    delete[] buf;

    if (!error_status && !ToolboxSendFileEnd(*dev)) {
        error_status = 19;
    }

    if (error_status) {
        fprintf(stderr, "An error occurred during the transfer, the destination file may have errors.\n");
    }

    _close(infile);

    return error_status;
}


static void PrintBanner(void)
{
    printf(
        "Toolbox for Emulated SCSI devices\n"
        "Copyright 2024 Niels Martin Hansen\n"
        "\n"
    );
}

static void PrintLicense(void)
{
    printf(
        "This program is free software, and you are welcome to redistribute it\n"
        "under certain conditions. This program comes with ABSOLUTELY NO WARRANTY.\n"
        "See the included COPYING file for details on the license.\n"
        "\n"
    );
}


static void PrintHelp(void)
{
    printf(
        "Usage:  SCSITB <command> [parameters]\n"
        "\n"
        "Commands:\n"
        "  info                    List all available SCSI adapters and devices.\n"
        "  lsimg <dev>             List available images for the given device.\n"
        "  setimg <dev> <idx>      Change the mounted image in the given device, to\n"
        "                          the image with the given index in the image list.\n"
        "  setname <dev> <name>    Change the mounted image in the given device, to\n"
        "                          the image with the given name.\n"
        "  lsdir <dev>             List shared directory for the given decice.\n"
        "  get <dev> <idx> [name]  Download a file from the shared directory.\n"
        "  put <dev> <filename>    Upload a file to the shared directory.\n"
        "\n"
        "Please see the documentation for more information about supported\n"
        "devices, how to configure your device for compatibility, etc.\n"
        "    Project:  https://github.com/nielsmh/escsitoolbox\n"
    );
}


int main(int argc, const char *argv[])
{
    int missingargs = 0;

    if (argc < 2) {
        PrintBanner();
        PrintHelp();
        return 1;
    }

    if (strcmpi(argv[1], "help") == 0 || strcmpi(argv[1], "h") == 0 ||
        strcmpi(argv[1], "-h") == 0 || strcmpi(argv[1], "-?") == 0 ||
        strcmpi(argv[1], "-help") == 0 || strcmpi(argv[1], "--help") == 0 ||
        strcmpi(argv[1], "/h") == 0 || strcmpi(argv[1], "/?") == 0) {
        /* So many ways to ask for help. Don't let the user down. */
        PrintBanner();
        PrintLicense();
        PrintHelp();
        return 0;
    }

    if (strcmpi(argv[1], "info") == 0) {
        return DoDeviceInfo(argc - 2, argv + 2);
    }

    if (strcmpi(argv[1], "lsimg") == 0) {
        if (argc >= 3) {
            return DoListImages(argc - 2, argv + 2);
        } else {
            missingargs = 1;
        }
    }

    if (strcmpi(argv[1], "setimg") == 0) {
        if (argc >= 4) {
            return DoSetImage(argc - 2, argv + 2);
        } else {
            missingargs = 2;
        }
    }

    if (strcmpi(argv[1], "setname") == 0) {
        if (argc >= 4) {
            return DoSetName(argc - 2, argv + 2);
        } else {
            missingargs = 2;
        }
    }

    if (strcmpi(argv[1], "lsdir") == 0) {
        if (argc >= 3) {
            return DoListSharedDir(argc - 2, argv + 2);
        } else {
            missingargs = 1;
        }
    }

    if (strcmpi(argv[1], "get") == 0) {
        if (argc >= 4) {
            return DoGetSharedDirFile(argc - 2, argv + 2);
        } else {
            missingargs = 2;
        }
    }

    if (strcmpi(argv[1], "put") == 0) {
        if (argc >= 4) {
            return DoPutSharedDirFile(argc - 2, argv + 2);
        } else {
            missingargs = 2;
        }
    }

    if (missingargs) {
        fprintf(stderr, "Missing parameters to command: %s\n\n", argv[1]);
        PrintHelp();
        return 1;
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
        PrintHelp();
        return 1;
    }
}


