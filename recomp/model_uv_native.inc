L_00429158:;
  /* The original second model-vertex pass, translated to native arithmetic.
     Keep the guest stack's call arguments and final x87 comparison status. */
  {
    double min_x = (double)rt_f32(R32(esp + 0x30u));
    double min_y = (double)rt_f32(R32(esp + 0x28u));
    double max_y = (double)rt_f32(R32(esp + 0x1cu));
    double range_x = (double)rt_f32(R32(esp + 0x24u));
    double range_y = (double)rt_f32(R32(esp + 0x10u));
    double offset_x = (double)rt_f32(R32(esp + 0x40u));
    double offset_y = (double)rt_f32(R32(esp + 0x44u));
    double lo = (double)rt_f32(R32(0x5b7b18u));
    double hi = (double)rt_f32(R32(0x5b7b14u));
    double wrap = (double)rt_f32(R32(0x5b7b10u));
    double special = rt_f64(R64(0x5b7b08u));
    double status_a = 0, status_b = 0;
    uint32_t n = R32(esi + 0x44u);
    for (uint32_t i = 0; i < n; ++i) {
      uint32_t dst = edi - 0x1cu;
      W32(esp - 4u, 0x40u);
      W32(esp - 8u, ecx);
      W32(esp - 12u, dst);
      W32(esp - 16u, 0x429164u);
      memmove(mb + dst, mb + ecx, 0x40u);
      double sx = (double)rt_f32(R32(ecx + 0x18u));
      double sy = (double)rt_f32(R32(ecx + 0x1cu));
      double xdiff = sx - min_x;
      double ydiff = sy - min_y;
      double xo;
      /* At 0x429164 ESP still includes 12 bytes of memcpy arguments:
         [ESP+0x30] is the caller's range_x at [ESP+0x24], not min_x. */
      if (range_x > lo && range_x < hi) {
        xo = xdiff * wrap;
        xo = xo / range_x;
      } else {
        xo = xdiff;
        if (range_x > hi && !(xdiff < hi)) xo -= wrap;
      }
      xo += offset_x;
      W32(edi - 4u, rt_u32f((float)xo));

      double yo;
      if (range_y > lo && range_y < hi) {
        yo = ydiff * wrap;
        yo = yo / range_y;
        status_a = range_y; status_b = hi;
        yo += offset_y;
      } else if (range_y > hi) {
        yo = ydiff;
        if (!(ydiff < hi)) yo -= wrap;
        status_a = ydiff; status_b = hi;
        yo += offset_y;
      } else {
        status_a = max_y; status_b = special;
        if (max_y == special) {
          yo = offset_y;
          yo += sy;
        } else {
          yo = ydiff;
          yo += offset_y;
        }
      }
      W32(edi, rt_u32f((float)yo));
      ecx += 0x40u;
      edi += 0x40u;
    }
    rt_fcom(c, status_a, status_b);
    eax = edx = n;
    W32(esp + 0x20u, ecx);
    W32(esp + 0x14u, eax);
    cf = of = sf = zf = 0; zf = pf = 1; fa = fb = eax; fr = 0;
  }
