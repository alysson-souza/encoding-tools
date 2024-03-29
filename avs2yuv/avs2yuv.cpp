// Avs2YUV by Loren Merritt

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include "internal.h"

#ifdef _MSC_VER
// what's up with MS's std libs?
#define dup _dup
#define popen _popen
#define pclose _pclose
#define fdopen _fdopen
#define setmode _setmode
#else
#include <unistd.h>
#endif

#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif

#define MY_VERSION "Avs2YUV 0.24"
#define MAX_FH 10

int __cdecl main(int argc, const char* argv[]) {
    const char* infile                 = nullptr;
    const char* hfyufile               = nullptr;
    const char* outfile[MAX_FH];
    int         y4m_headers[MAX_FH];
    FILE*       out_fh[10];
    int         out_fhs                = 0;
    bool        verbose                = 0;
    bool        usage                  = 0;
    int         seek                   = 0;
    int         end                    = 0;
    int         slave                  = 0;
    int         rawyuv                 = 0;
    int         frm                    = -1;
    int         write_target           = 0; // how many bytes per frame we expect to write

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != 0) {
            if (!strcmp(argv[i], "-v"))
                verbose = true;
            else if (!strcmp(argv[i], "-h"))
                usage = true;
            else if (!strcmp(argv[i], "-o")) {
                if (i > argc-2) {
                    fprintf_s(stderr, "-o needs an argument\n");
                    return 2;
                }
                i++;
                goto add_outfile;
            } else if (!strcmp(argv[i], "-seek")) {
                if (i > argc-2) {
                    fprintf_s(stderr, "-seek needs an argument\n");
                    return 2;
                }
                seek = atoi(argv[++i]);
                if (seek < 0) usage = 1;
            } else if (!strcmp(argv[i], "-frames")) {
                if (i > argc-2) {
                    fprintf_s(stderr, "-frames needs an argument\n");
                    return 2;
                }
                end = atoi(argv[++i]);
            } else if (!strcmp(argv[i], "-hfyu")) {
                if (i > argc-2) {
                    fprintf_s(stderr, "-hfyu needs an argument\n");
                    return 2;
                }
                hfyufile = argv[++i];
            } else if (!strcmp(argv[i], "-raw")) {
                rawyuv = 1;
            } else if (!strcmp(argv[i], "-slave")) {
                slave = 1;
            } else {
                fprintf_s(stderr, "no such option: %s\n", argv[i]);
                return 2;
            }
        } else if (!infile) {
            infile = argv[i];
            const char *dot = strrchr(infile, '.');
            if (!dot || strcmp(".avs", dot)) {
                fprintf_s(stderr, "infile (%s) doesn't look like an avisynth script\n", infile);
			}
        } else {
add_outfile:
            if (out_fhs > MAX_FH-1) {
                fprintf_s(stderr, "too many output files\n");
                return 2;
            }
            outfile[out_fhs] = argv[i];
            y4m_headers[out_fhs] = !rawyuv;
            out_fhs++;
        }
    }

    if (usage || !infile || (!out_fhs && !hfyufile && !verbose)) {
        fprintf_s(stderr, MY_VERSION "\n"
                  "Usage: avs2yuv [options] in.avs [-o out.y4m] [-o out2.y4m] [-hfyu out.avi]\n"
                  "-v\tprint the frame number after processing each frame\n"
                  "-seek\tseek to the given frame number\n"
                  "-frames\tstop after processing this many frames\n"
                  "-slave\tread a list of frame numbers from stdin (one per line)\n"
                  "-raw\toutputs raw I420 instead of yuv4mpeg\n"
                  "The outfile may be \"-\", meaning stdout.\n"
                  "Output format is yuv4mpeg, as used by MPlayer and mjpegtools\n"
                  "Huffyuv output requires MEncoder, and probably doesn't work in Wine.\n"
                 );
        return 2;
    }

    try {
        HMODULE avsdll = LoadLibrary("avisynth.dll");
        if (!avsdll) {
            fprintf_s(stderr, "failed to load avisynth.dll\n");
            return 2;
        }
        IScriptEnvironment* (* CreateScriptEnvironment)(int version)
        = (IScriptEnvironment*(*)(int)) GetProcAddress(avsdll, "CreateScriptEnvironment");
        if (!CreateScriptEnvironment) {
            fprintf_s(stderr, "failed to load CreateScriptEnvironment()\n");
            return 1;
        }

        IScriptEnvironment* env = CreateScriptEnvironment(AVISYNTH_INTERFACE_VERSION);
        std::shared_ptr<IScriptEnvironment> pEnv(env);
        AVSValue arg(infile);
        AVSValue res = pEnv->Invoke("Import", AVSValue(&arg, 1));
        if (!res.IsClip()) {
            fprintf_s(stderr, "Error: '%s' didn't return a video clip.\n", infile);
            return 1;
        }
        PClip clip = res.AsClip();
        VideoInfo inf = clip->GetVideoInfo();

        fprintf_s(stderr, "%s: %dx%d, ", infile, inf.width, inf.height);
        if (inf.fps_denominator == 1)
            fprintf_s(stderr, "%u fps, ", inf.fps_numerator);
        else
            fprintf_s(stderr, "%u/%u fps, ", inf.fps_numerator, inf.fps_denominator);
        fprintf_s(stderr, "%d frames\n", inf.num_frames);

        if (!inf.IsYV12()) {
            fprintf_s(stderr, "converting %s -> YV12\n", inf.IsYUY2() ? "YUY2" : inf.IsRGB() ? "RGB" : "?");
            res = pEnv->Invoke("converttoyv12", AVSValue(&res, 1));
            clip = res.AsClip();
            inf = clip->GetVideoInfo();
        }
        if (!inf.IsYV12()) {
            fprintf_s(stderr, "Couldn't convert input to YV12\n");
            return 1;
        }
        if (inf.IsFieldBased()) {
            fprintf_s(stderr, "Needs progressive input\n");
            return 1;
        }

        for (int i = 0; i < out_fhs; i++) {
            if (!strcmp(outfile[i], "-")) {
                for (int j=0; j<i; j++)
                    if (out_fh[j] == stdout) {
                        fprintf_s(stderr, "can't write to stdout multiple times\n");
                        return 2;
                    }
                int dupout = dup(_fileno(stdout));
                fclose(stdout);
                setmode(dupout, O_BINARY);
                out_fh[i] = fdopen(dupout, "wb");
            } else {
                errno_t err = fopen_s(&out_fh[i], outfile[i], "wb");
                if (err != 0) {
                    fprintf_s(stderr, "fopen(\"%s\") failed", outfile);
                    return 1;
                }
            }
        }
        if (hfyufile) {
            char *cmd = new char[100+strlen(hfyufile)];
            sprintf_s(cmd, sizeof(cmd), "mencoder - -o \"%s\" -quiet -ovc lavc -lavcopts vcodec=ffvhuff:vstrict=-1:pred=2:context=1", hfyufile);
            out_fh[out_fhs] = popen(cmd, "wb");
            if (!out_fh[out_fhs]) {
                fprintf_s(stderr, "failed to exec mencoder\n");
                return 1;
            }
            y4m_headers[out_fhs] = 1;
            out_fhs++;
            delete [] cmd;
        }

        for (int i = 0; i < out_fhs; i++) {
            if (!y4m_headers[i])
                continue;
            fprintf_s(out_fh[i], "YUV4MPEG2 W%d H%d F%lu:%lu Ip A0:0\n",
                      inf.width, inf.height, inf.fps_numerator, inf.fps_denominator);
            fflush(out_fh[i]);
        }

        write_target = out_fhs*inf.width*inf.height*3/2;

        if (slave) {
            seek = 0;
            end = INT_MAX;
        } else {
            end += seek;
            if (end <= seek || end > inf.num_frames)
                end = inf.num_frames;
        }

        for (frm = seek; frm < end; ++frm) {
            if (slave) {
                char input[80];
                frm = -1;
                do {
                    if (!fgets(input, 80, stdin))
                        goto close_files;
                    sscanf_s(input, "%d", &frm);
                } while (frm < 0);
                if (frm >= inf.num_frames)
                    frm = inf.num_frames-1;
            }

            PVideoFrame f = clip->GetFrame(frm, pEnv.get());

            if (out_fhs) {
                static const int planes[] = {PLANAR_Y, PLANAR_U, PLANAR_V};
                int wrote = 0;

                for (int i = 0; i < out_fhs; i++)
                    if (y4m_headers[i])
                        fwrite("FRAME\n", 1, 6, out_fh[i]);

                for (int p = 0; p < 3; p++) {
                    int w = inf.width  >> (p ? 1 : 0);
                    int h = inf.height >> (p ? 1 : 0);
                    int pitch = f->GetPitch(planes[p]);
                    const BYTE* data = f->GetReadPtr(planes[p]);
                    int y;
                    for (y = 0; y < h; y++) {
                        for (int i = 0; i < out_fhs; i++)
                            wrote += fwrite(data, 1, w, out_fh[i]);
                        data += pitch;
                    }
                }
                if (wrote != write_target) {
                    fprintf_s(stderr, "Output error: wrote only %d of %d bytes\n", wrote, write_target);
                    return 1;
                }
                if (slave) { // assume timing doesn't matter in other modes
                    for (int i = 0; i < out_fhs; i++)
                        fflush(out_fh[i]);
                }
            }

            if (verbose)
                fprintf_s(stderr, "%d\n", frm);
        }
    } catch (AvisynthError err) {
        if (frm >= 0)
            fprintf_s(stderr, "\nAvisynth error at frame %d:\n%s\n", frm, err.msg);
        else
            fprintf_s(stderr, "\nAvisynth error:\n%s\n", err.msg);
        return 1;
    }

close_files:
    if (hfyufile) {
        pclose(out_fh[out_fhs-1]);
        out_fhs--;
    }
    for (int i = 0; i < out_fhs; i++)
        fclose(out_fh[i]);
    return 0;
}
