    .text
    .align

    .global blend_asm_neon

     # DOES NOT WORK
blend_asm_neon:

      # r0: Ptr to data
      # r1: Iteration count:

      push        {r4-r6,lr}

      mov         r5, r0
      mov         r6, r1

      # r3 - value to bled to - ALPHA*value2, 220*0x1B
      # r4 - alpha level from (255 - ALPHA)
      movw        r3, #5940
      mov         r4, #35
      vdup.8      d4, r4

  .loop:
      # load 8 pixels
      vld4.8      {d0-d3}, [r5]

      # load initial values into registers
      vdup.16     q0, r3
      vdup.16     q1, r3
      vdup.16     q2, r3

      # multiply
      vmull.u8    q0, d0, d4
      vmull.u8    q1, d1, d4
      vmull.u8    q2, d2, d4

      # shift and store:
      vshr.u8   d0, q0, #8
      vshr.u8   d1, q1, #8
      vshr.u8   d2, q2, #8

      vst4.8      {d0-d3}, [r5]!


      subs        r6, r6, #1
      bne         .loop

      pop         { r4-r6, pc }
