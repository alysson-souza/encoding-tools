
vinverse is based off the Vinverse script function by Did�e, which is a
small but effective function against residual combing.

    original post:  

         http://forum.doom9.org/showthread.php?p=841641&highlight=vinverse#post841641

This filter supports YV12 and YUY2 input.

syntax =>

    vinverse(clip, float sstr, int amnt, int uv, float scl, int opt)

parameters =>

    sstr: strength of contra sharpening  (default = 2.7)

    amnt: change no pixel by more than this  (default = 255, range:  0 < amnt <= 255)

    uv  : chroma mode, as in MaskTools:  1=trash chroma, 2=pass chroma through, 3=process chroma  (default = 3)

    scl : scale factor for VshrpD*VblurD < 0  (default = 0.25)

    opt : 0 = use c, 1 = use mmx, 2 = auto detect  (default = 2, mmx and c routines give the same output)