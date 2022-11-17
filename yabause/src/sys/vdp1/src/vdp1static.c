static Vdp1 latchedRegs;
static int nbCmdToProcess = 0;
static int CmdListInLoop = 0;
static int CmdListLimit = 0x80000;
static int needVdp1draw = 0;
static u32 returnAddr = 0xffffffff;
static int lastHash = -1;
static vdp1cmd_struct * usrClipCmd = NULL;
static vdp1cmd_struct * sysClipCmd = NULL;
static vdp1cmd_struct * localCoordCmd = NULL;

//////////////////////////////////////////////////////////////////////////////
static void RequestVdp1ToDraw() {
  needVdp1draw = 1;
}
//////////////////////////////////////////////////////////////////////////////
static void abortVdp1() {
  if (Vdp1External.status == VDP1_STATUS_RUNNING) {
    // The vdp1 is still running and a new draw command request has been received
    // Abort the current command list
    Vdp1External.status = VDP1_STATUS_IDLE;
    CmdListInLoop = 0;
    vdp1_clock = 0;
    nbCmdToProcess = 0;
    needVdp1draw = 0;
  }
}
//////////////////////////////////////////////////////////////////////////////
static int needVBlankErase() {
  return (Vdp1External.useVBlankErase != 0);
}
//////////////////////////////////////////////////////////////////////////////
static void updateTVMRMode() {
  Vdp1External.useVBlankErase = 0;
  if (((Vdp1Regs->regs.FBCR & 3) == 3) && (((Vdp1Regs->regs.TVMR >> 3) & 0x01) == 1)) {
    Vdp1External.useVBlankErase = 1;
  } else {
    if ((((Vdp1Regs->regs.TVMR >> 3) & 0x01) == 1)) {
      //VBE can be one only when FCM and FCT are 1
      LOG("Prohibited FBCR/TVMR values\n");
      // Assume prohibited modes behave like if VBE/FCT/FCM were all 1
      Vdp1External.manualchange = 1;
      Vdp1External.useVBlankErase = 1;
    }
  }
}
//////////////////////////////////////////////////////////////////////////////
static void updateFBCRMode() {
  Vdp1External.manualchange = 0;
  Vdp1External.onecyclemode = 0;
  Vdp1External.useVBlankErase = 0;
  if (((Vdp1Regs->regs.TVMR >> 3) & 0x01) == 1){ //VBE is set
    if ((Vdp1Regs->regs.FBCR & 3) == 3) {
      Vdp1External.manualchange = 1;
      Vdp1External.useVBlankErase = 1;
    } else {
      //VBE can be one only when FCM and FCT are 1
      LOG("Prohibited FBCR/TVMR values\n");
      // Assume prohibited modes behave like if VBE/FCT/FCM were all 1
      Vdp1External.manualchange = 1;
      Vdp1External.useVBlankErase = 1;
    }
  } else {
    //Manual erase shall not be reseted but need to save its current value
    // Only at frame change the order is executed.
    //This allows to have both a manual clear and a manual change at the same frame without continuously clearing the VDP1
    //The mechanism is used by the official bios animation
    Vdp1External.onecyclemode = ((Vdp1Regs->regs.FBCR & 3) == 0) || ((Vdp1Regs->regs.FBCR & 3) == 1);
    Vdp1External.manualerase |= ((Vdp1Regs->regs.FBCR & 3) == 2);
    Vdp1External.manualchange = ((Vdp1Regs->regs.FBCR & 3) == 3);
  }
}
//////////////////////////////////////////////////////////////////////////////
static void printCommand(vdp1cmd_struct *cmd) {
  printf("===== CMD =====\n");
  printf("CMDCTRL = 0x%x\n",cmd->CMDCTRL );
  printf("CMDLINK = 0x%x\n",cmd->CMDLINK );
  printf("CMDPMOD = 0x%x\n",cmd->CMDPMOD );
  printf("CMDCOLR = 0x%x\n",cmd->CMDCOLR );
  printf("CMDSRCA = 0x%x\n",cmd->CMDSRCA );
  printf("CMDSIZE = 0x%x\n",cmd->CMDSIZE );
  printf("CMDXA = 0x%x\n",cmd->CMDXA );
  printf("CMDYA = 0x%x\n",cmd->CMDYA );
  printf("CMDXB = 0x%x\n",cmd->CMDXB );
  printf("CMDYB = 0x%x\n",cmd->CMDYB );
  printf("CMDXC = 0x%x\n",cmd->CMDXC );
  printf("CMDYC = 0x%x\n",cmd->CMDYC );
  printf("CMDXD = 0x%x\n",cmd->CMDXD );
  printf("CMDYD = 0x%x\n",cmd->CMDYD );
  printf("CMDGRDA = 0x%x\n",cmd->CMDGRDA );
}
//////////////////////////////////////////////////////////////////////////////
static int emptyCmd(vdp1cmd_struct *cmd) {
  return (
    (cmd->CMDCTRL == 0) &&
    (cmd->CMDLINK == 0) &&
    (cmd->CMDPMOD == 0) &&
    (cmd->CMDCOLR == 0) &&
    (cmd->CMDSRCA == 0) &&
    (cmd->CMDSIZE == 0) &&
    (cmd->CMDXA == 0) &&
    (cmd->CMDYA == 0) &&
    (cmd->CMDXB == 0) &&
    (cmd->CMDYB == 0) &&
    (cmd->CMDXC == 0) &&
    (cmd->CMDYC == 0) &&
    (cmd->CMDXD == 0) &&
    (cmd->CMDYD == 0) &&
    (cmd->CMDGRDA == 0));
}
//////////////////////////////////////////////////////////////////////////////
static void checkClipCmd(vdp1cmd_struct **sysClipCmd, vdp1cmd_struct **usrClipCmd, vdp1cmd_struct **localCoordCmd, u8 * ram, Vdp1 * regs) {
  if (sysClipCmd != NULL) {
    if (*sysClipCmd != NULL) {
      VIDCore->Vdp1SystemClipping(*sysClipCmd, ram, regs);
      free(*sysClipCmd);
      *sysClipCmd = NULL;
    }
  }
  if (usrClipCmd != NULL) {
    if (*usrClipCmd != NULL) {
      VIDCore->Vdp1UserClipping(*usrClipCmd, ram, regs);
      free(*usrClipCmd);
      *usrClipCmd = NULL;
    }
  }
  if (localCoordCmd != NULL) {
    if (*localCoordCmd != NULL) {
      VIDCore->Vdp1LocalCoordinate(*localCoordCmd, ram, regs);
      free(*localCoordCmd);
      *localCoordCmd = NULL;
    }
  }
}
//////////////////////////////////////////////////////////////////////////////
static int Vdp1NormalSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs, u8* back_framebuffer){
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];
  int ret = 1;
  if (emptyCmd(cmd)) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return -1;
  }

  if ((cmd->CMDSIZE & 0x8000)) {
    yabsys.vdp1cycles += 70;
    regs->regs.EDSR |= 2;
    return -1; // BAD Command
  }
  if (((cmd->CMDPMOD >> 3) & 0x7) > 5) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return -1;
  }
  cmd->w = ((cmd->CMDSIZE >> 8) & 0x3F) * 8;
  cmd->h = cmd->CMDSIZE & 0xFF;
  if ((cmd->w == 0) || (cmd->h == 0)) {
    yabsys.vdp1cycles += 70;
    ret = 0;
  }

  cmd->flip = (cmd->CMDCTRL & 0x30) >> 4;
  cmd->priority = 0;

  CONVERTCMD(cmd->CMDXA);
  CONVERTCMD(cmd->CMDYA);
  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;

  cmd->CMDXB = cmd->CMDXA + MAX(1,cmd->w);
  cmd->CMDYB = cmd->CMDYA;
  cmd->CMDXC = cmd->CMDXA + MAX(1,cmd->w);
  cmd->CMDYC = cmd->CMDYA + MAX(1,cmd->h);
  cmd->CMDXD = cmd->CMDXA;
  cmd->CMDYD = cmd->CMDYA + MAX(1,cmd->h);

  int area = abs((cmd->CMDXA*cmd->CMDYB - cmd->CMDXB*cmd->CMDYA) + (cmd->CMDXB*cmd->CMDYC - cmd->CMDXC*cmd->CMDYB) + (cmd->CMDXC*cmd->CMDYD - cmd->CMDXD*cmd->CMDYC) + (cmd->CMDXD*cmd->CMDYA - cmd->CMDXA *cmd->CMDYD))/2;
  switch ((cmd->CMDPMOD >> 3) & 0x7) {
    case 0:
    case 1:
      // 4 pixels per 16 bits
      area  = area >> 2;
      break;
    case 2:
    case 3:
    case 4:
      // 2 pixels per 16 bits
      area = area >> 1;
      break;
    default:
      break;
  }
  yabsys.vdp1cycles+= MIN(1000, 70 + (area));

  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    yabsys.vdp1cycles+= 232;
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, ram, (Vdp1RamReadWord(NULL, ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }

  VIDCore->Vdp1NormalSpriteDraw(cmd, ram, regs, back_framebuffer);
  return ret;
}
//////////////////////////////////////////////////////////////////////////////
static int Vdp1ScaledSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs, u8* back_framebuffer) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];
  s16 rw = 0, rh = 0;
  s16 x, y;
  int ret = 1;

  if (emptyCmd(cmd)) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return -1;
  }

  cmd->w = ((cmd->CMDSIZE >> 8) & 0x3F) * 8;
  cmd->h = cmd->CMDSIZE & 0xFF;
  if ((cmd->w == 0) || (cmd->h == 0)) {
    yabsys.vdp1cycles += 70;
    ret = 0;
  }

  cmd->flip = (cmd->CMDCTRL & 0x30) >> 4;
  cmd->priority = 0;

  CONVERTCMD(cmd->CMDXA);
  CONVERTCMD(cmd->CMDYA);
  CONVERTCMD(cmd->CMDXB);
  CONVERTCMD(cmd->CMDYB);
  CONVERTCMD(cmd->CMDXC);
  CONVERTCMD(cmd->CMDYC);

  x = cmd->CMDXA;
  y = cmd->CMDYA;
  // Setup Zoom Point
  switch ((cmd->CMDCTRL & 0xF00) >> 8)
  {
  case 0x0: // Only two coordinates
    rw = cmd->CMDXC - cmd->CMDXA;
    rh = cmd->CMDYC - cmd->CMDYA;
    break;
  case 0x5: // Upper-left
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    break;
  case 0x6: // Upper-Center
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw / 2;
    break;
  case 0x7: // Upper-Right
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw;
    break;
  case 0x9: // Center-left
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    y = y - rh / 2;
    break;
  case 0xA: // Center-center
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw / 2;
    y = y - rh / 2;
    break;
  case 0xB: // Center-right
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw;
    y = y - rh / 2;
    break;
  case 0xD: // Lower-left
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    y = y - rh;
    break;
  case 0xE: // Lower-center
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw / 2;
    y = y - rh;
    break;
  case 0xF: // Lower-right
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw;
    y = y - rh;
    break;
  default: break;
  }

  cmd->CMDXA = x + regs->localX;
  cmd->CMDYA = y + regs->localY;
  cmd->CMDXB = x + rw  + regs->localX;
  cmd->CMDYB = y + regs->localY;
  cmd->CMDXC = x + rw  + regs->localX;
  cmd->CMDYC = y + rh + regs->localY;
  cmd->CMDXD = x + regs->localX;
  cmd->CMDYD = y + rh + regs->localY;

  // Setup Zoom Point
  switch ((cmd->CMDCTRL & 0xF00) >> 8)
  {
  case 0x0: // Only two coordinates
    if ((s16)cmd->CMDXC > (s16)cmd->CMDXA){ cmd->CMDXB += 1; cmd->CMDXC += 1;} else { cmd->CMDXA += 1; cmd->CMDXB +=1; cmd->CMDXC += 1; cmd->CMDXD += 1;}
    if ((s16)cmd->CMDYC > (s16)cmd->CMDYA){ cmd->CMDYC += 1; cmd->CMDYD += 1;} else { cmd->CMDYA += 1; cmd->CMDYB += 1; cmd->CMDYC += 1; cmd->CMDYD += 1;}
    break;
  case 0x5: // Upper-left
  case 0x6: // Upper-Center
  case 0x7: // Upper-Right
  case 0x9: // Center-left
  case 0xA: // Center-center
  case 0xB: // Center-right
  case 0xD: // Lower-left
  case 0xE: // Lower-center
  case 0xF: // Lower-right
    cmd->CMDXB += 1;
    cmd->CMDXC += 1;
    cmd->CMDYC += 1;
    cmd->CMDYD += 1;
    break;
  default: break;
  }

  int area = abs((cmd->CMDXA*cmd->CMDYB - cmd->CMDXB*cmd->CMDYA) + (cmd->CMDXB*cmd->CMDYC - cmd->CMDXC*cmd->CMDYB) + (cmd->CMDXC*cmd->CMDYD - cmd->CMDXD*cmd->CMDYC) + (cmd->CMDXD*cmd->CMDYA - cmd->CMDXA *cmd->CMDYD))/2;
  switch ((cmd->CMDPMOD >> 3) & 0x7) {
    case 0:
    case 1:
      // 4 pixels per 16 bits
      area  = area >> 2;
      break;
    case 2:
    case 3:
    case 4:
      // 2 pixels per 16 bits
      area = area >> 1;
      break;
    default:
      break;
  }
  yabsys.vdp1cycles+= MIN(1000, 70 + area);

  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    yabsys.vdp1cycles+= 232;
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }

  VIDCore->Vdp1ScaledSpriteDraw(cmd, ram, regs, back_framebuffer);
  return ret;
}
//////////////////////////////////////////////////////////////////////////////
static int Vdp1DistortedSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs, u8* back_framebuffer) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];
  int ret = 1;

  if (emptyCmd(cmd)) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return 0;
  }

  cmd->w = ((cmd->CMDSIZE >> 8) & 0x3F) * 8;
  cmd->h = cmd->CMDSIZE & 0xFF;
  if ((cmd->w == 0) || (cmd->h == 0)) {
    yabsys.vdp1cycles += 70;
    ret = 0;
  }

  cmd->flip = (cmd->CMDCTRL & 0x30) >> 4;
  cmd->priority = 0;

  CONVERTCMD(cmd->CMDXA);
  CONVERTCMD(cmd->CMDYA);
  CONVERTCMD(cmd->CMDXB);
  CONVERTCMD(cmd->CMDYB);
  CONVERTCMD(cmd->CMDXC);
  CONVERTCMD(cmd->CMDYC);
  CONVERTCMD(cmd->CMDXD);
  CONVERTCMD(cmd->CMDYD);

  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC += regs->localX;
  cmd->CMDYC += regs->localY;
  cmd->CMDXD += regs->localX;
  cmd->CMDYD += regs->localY;

  int area = abs((cmd->CMDXA*cmd->CMDYB - cmd->CMDXB*cmd->CMDYA) + (cmd->CMDXB*cmd->CMDYC - cmd->CMDXC*cmd->CMDYB) + (cmd->CMDXC*cmd->CMDYD - cmd->CMDXD*cmd->CMDYC) + (cmd->CMDXD*cmd->CMDYA - cmd->CMDXA *cmd->CMDYD))/2;
  switch ((cmd->CMDPMOD >> 3) & 0x7) {
    case 0:
    case 1:
      // 4 pixels per 16 bits
      area  = area >> 2;
      break;
    case 2:
    case 3:
    case 4:
      // 2 pixels per 16 bits
      area = area >> 1;
      break;
    default:
      break;
  }
  yabsys.vdp1cycles+= MIN(1000, 70 + (area*3));

  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    yabsys.vdp1cycles+= 232;
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }

  VIDCore->Vdp1DistortedSpriteDraw(cmd, ram, regs, back_framebuffer);
  return ret;
}
//////////////////////////////////////////////////////////////////////////////
static int Vdp1PolygonDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs, u8* back_framebuffer) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];

  CONVERTCMD(cmd->CMDXA);
  CONVERTCMD(cmd->CMDYA);
  CONVERTCMD(cmd->CMDXB);
  CONVERTCMD(cmd->CMDYB);
  CONVERTCMD(cmd->CMDXC);
  CONVERTCMD(cmd->CMDYC);
  CONVERTCMD(cmd->CMDXD);
  CONVERTCMD(cmd->CMDYD);

  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC += regs->localX;
  cmd->CMDYC += regs->localY;
  cmd->CMDXD += regs->localX;
  cmd->CMDYD += regs->localY;

  int w = (sqrt((cmd->CMDXA - cmd->CMDXB)*(cmd->CMDXA - cmd->CMDXB)) + sqrt((cmd->CMDXD - cmd->CMDXC)*(cmd->CMDXD - cmd->CMDXC)))/2;
  int h = (sqrt((cmd->CMDYA - cmd->CMDYD)*(cmd->CMDYA - cmd->CMDYD)) + sqrt((cmd->CMDYB - cmd->CMDYC)*(cmd->CMDYB - cmd->CMDYC)))/2;
  yabsys.vdp1cycles += MIN(1000, 16 + (w * h) + (w * 2));

  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    yabsys.vdp1cycles+= 232;
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }
  cmd->priority = 0;
  cmd->w = 1;
  cmd->h = 1;
  cmd->flip = 0;

  VIDCore->Vdp1PolygonDraw(cmd, ram, regs, back_framebuffer);
  return 1;
}
//////////////////////////////////////////////////////////////////////////////
static int Vdp1PolylineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs, u8* back_framebuffer) {

  Vdp2 *varVdp2Regs = &Vdp2Lines[0];

  cmd->priority = 0;
  cmd->w = 1;
  cmd->h = 1;
  cmd->flip = 0;

  CONVERTCMD(cmd->CMDXA);
  CONVERTCMD(cmd->CMDYA);
  CONVERTCMD(cmd->CMDXB);
  CONVERTCMD(cmd->CMDYB);
  CONVERTCMD(cmd->CMDXC);
  CONVERTCMD(cmd->CMDYC);
  CONVERTCMD(cmd->CMDXD);
  CONVERTCMD(cmd->CMDYD);

  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC += regs->localX;
  cmd->CMDYC += regs->localY;
  cmd->CMDXD += regs->localX;
  cmd->CMDYD += regs->localY;

  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }
  VIDCore->Vdp1PolylineDraw(cmd, ram, regs, back_framebuffer);

  return 1;
}
//////////////////////////////////////////////////////////////////////////////
static int Vdp1LineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs, u8* back_framebuffer) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];

  CONVERTCMD(cmd->CMDXA);
  CONVERTCMD(cmd->CMDYA);
  CONVERTCMD(cmd->CMDXB);
  CONVERTCMD(cmd->CMDYB);

  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC = cmd->CMDXB;
  cmd->CMDYC = cmd->CMDYB;
  cmd->CMDXD = cmd->CMDXA;
  cmd->CMDYD = cmd->CMDYA;

  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
  for (int i = 0; i < 4; i++){
    u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
    cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
    cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
    cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
  }
  }
  cmd->priority = 0;
  cmd->w = 1;
  cmd->h = 1;
  cmd->flip = 0;

  VIDCore->Vdp1LineDraw(cmd, ram, regs, back_framebuffer);

  return 1;
}
//////////////////////////////////////////////////////////////////////////////
static void setupSpriteLimit(vdp1cmdctrl_struct *ctrl){
  vdp1cmd_struct *cmd = &ctrl->cmd;
  u32 dot;
  switch ((cmd->CMDPMOD >> 3) & 0x7)
  {
  case 0:
  {
    // 4 bpp Bank mode
    ctrl->start_addr = cmd->CMDSRCA * 8;
    ctrl->end_addr = ctrl->start_addr + MAX(1,cmd->h)*MAX(1,cmd->w)/2;
    break;
  }
  case 1:
  {
    // 4 bpp LUT mode
    u32 colorLut = cmd->CMDCOLR * 8;
    u32 charAddr = cmd->CMDSRCA * 8;
    ctrl->start_addr = cmd->CMDSRCA * 8;
    ctrl->end_addr = ctrl->start_addr + MAX(1,cmd->h)*MAX(1,cmd->w)/2;

    for (int i = 0; i < MAX(1,cmd->h); i++)
    {
      u16 j;
      j = 0;
      while (j < MAX(1,cmd->w)/2)
      {
        dot = Vdp1RamReadByte(NULL, Vdp1Ram, charAddr);
        int lutaddr = (dot >> 4) * 2 + colorLut;
        ctrl->start_addr = (ctrl->start_addr > lutaddr)?lutaddr:ctrl->start_addr;
        ctrl->end_addr = (ctrl->end_addr < lutaddr)?lutaddr:ctrl->end_addr;
        charAddr += 1;
        j+=1;
      }
    }
    break;
  }
  case 2:
  case 3:
  case 4:
  {
    // 8 bpp(64 color) Bank mode
    ctrl->start_addr = cmd->CMDSRCA * 8;
    ctrl->end_addr = ctrl->start_addr + MAX(1,cmd->h)*MAX(1,cmd->w);
    break;
  }
  case 5:
  {
    // 16 bpp Bank mode
    // 8 bpp(64 color) Bank mode
    ctrl->start_addr = cmd->CMDSRCA * 8;
    ctrl->end_addr = ctrl->start_addr + MAX(1,cmd->h)*MAX(1,cmd->w)*2;
    break;
  }
  default:
    VDP1LOG("Unimplemented sprite color mode: %X\n", (cmd->CMDPMOD >> 3) & 0x7);
    break;
   }
}
//////////////////////////////////////////////////////////////////////////////
static int getVdp1CyclesPerLine(void)
{
  int clock = 26842600;
  int fps = 60;
  //Using p37, Table 4.2 of vdp1 official doc
  if (yabsys.IsPal) {
    fps = 50;
    // Horizontal Resolution
    switch (Vdp2Lines[0].TVMD & 0x7)
    {
    case 0:
    case 2:
    case 4:
    case 6:
      //W is 320 or 640
      clock = 26656400;
      break;
    case 1:
    case 3:
    case 5:
    case 7:
      //W is 352 or 704
      clock = 28437500;
      break;
    }
  } else {
    // Horizontal Resolution
    switch (Vdp2Lines[0].TVMD & 0x7)
    {
    case 0:
    case 2:
    case 4:
    case 6:
      //W is 320 or 640
      clock = 26842600;
      break;
    case 1:
    case 3:
    case 5:
    case 7:
      //W is 352 or 704
      clock = 28636400;
      break;
    }
  }
  return clock/(fps*yabsys.MaxLineCount);
}
//////////////////////////////////////////////////////////////////////////////
static void FASTCALL Vdp1ReadCommand(vdp1cmd_struct *cmd, u32 addr, u8* ram) {
  cmd->CMDCTRL = T1ReadWord(ram, addr);
  cmd->CMDLINK = T1ReadWord(ram, addr + 0x2);
  cmd->CMDPMOD = T1ReadWord(ram, addr + 0x4);
  cmd->CMDCOLR = T1ReadWord(ram, addr + 0x6);
  cmd->CMDSRCA = T1ReadWord(ram, addr + 0x8);
  cmd->CMDSIZE = T1ReadWord(ram, addr + 0xA);
  cmd->CMDXA = T1ReadWord(ram, addr + 0xC);
  cmd->CMDYA = T1ReadWord(ram, addr + 0xE);
  cmd->CMDXB = T1ReadWord(ram, addr + 0x10);
  cmd->CMDYB = T1ReadWord(ram, addr + 0x12);
  cmd->CMDXC = T1ReadWord(ram, addr + 0x14);
  cmd->CMDYC = T1ReadWord(ram, addr + 0x16);
  cmd->CMDXD = T1ReadWord(ram, addr + 0x18);
  cmd->CMDYD = T1ReadWord(ram, addr + 0x1A);
  cmd->CMDGRDA = T1ReadWord(ram, addr + 0x1C);
}
//////////////////////////////////////////////////////////////////////////////
static int EvaluateCmdListHash(Vdp1 * regs){
  int hash = 0;
  u32 addr = 0;
  u32 returnAddr = 0xFFFFFFFF;
  u32 commandCounter = 0;
  u16 command;

  command = T1ReadWord(Vdp1Ram, addr);

  while (!(command & 0x8000) && (commandCounter < 2000))
  {
      vdp1cmd_struct cmd;
     // Make sure we're still dealing with a valid command
     if ((command & 0x000C) == 0x000C)
        // Invalid, abort
        return hash;
      Vdp1ReadCommand(&cmd, addr, Vdp1Ram);
      hash ^= (cmd.CMDCTRL << 16) | cmd.CMDLINK;
      hash ^= (cmd.CMDPMOD << 16) | cmd.CMDCOLR;
      hash ^= (cmd.CMDSRCA << 16) | cmd.CMDSIZE;
      hash ^= (cmd.CMDXA << 16) | cmd.CMDYA;
      hash ^= (cmd.CMDXB << 16) | cmd.CMDYB;
      hash ^= (cmd.CMDXC << 16) | cmd.CMDYC;
      hash ^= (cmd.CMDXD << 16) | cmd.CMDYD;
      hash ^= (cmd.CMDGRDA << 16) | _Ygl->drawframe;

     // Determine where to go next
     switch ((command & 0x3000) >> 12)
     {
        case 0: // NEXT, jump to following table
           addr += 0x20;
           break;
        case 1: // ASSIGN, jump to CMDLINK
           addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
           break;
        case 2: // CALL, call a subroutine
           if (returnAddr == 0xFFFFFFFF)
              returnAddr = addr + 0x20;

           addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
           break;
        case 3: // RETURN, return from subroutine
           if (returnAddr != 0xFFFFFFFF) {
              addr = returnAddr;
              returnAddr = 0xFFFFFFFF;
           }
           else
              addr += 0x20;
           break;
     }

     if (addr > 0x7FFE0)
        return hash;
     command = T1ReadWord(Vdp1Ram, addr);
     commandCounter++;
  }
  return hash;
}
//////////////////////////////////////////////////////////////////////////////
static int sameCmd(vdp1cmd_struct* a, vdp1cmd_struct* b) {
  if (a == NULL) return 0;
  if (b == NULL) return 0;
  if (emptyCmd(a)) return 0;
  int cmp = memcmp(a, b, 15*sizeof(int));
  if (cmp == 0) {
    return 1;
  }
  return 0;
}
//////////////////////////////////////////////////////////////////////////////
static void Vdp1NoDraw(void) {
  // beginning of a frame (ST-013-R3-061694 page 53)
  // BEF <- CEF
  // CEF <- 0
  //Vdp1Regs->regs.EDSR >>= 1;
  /* this should be done after a frame change or a plot trigger */
  Vdp1Regs->regs.COPR = 0;
  Vdp1Regs->lCOPR = 0;
  _Ygl->vdp1On[_Ygl->drawframe] = 0;
  Vdp1FakeDrawCommands(Vdp1Ram, Vdp1Regs);
}
//////////////////////////////////////////////////////////////////////////////
static int Vdp1Draw(void)
{
  FRAMELOG("Vdp1Draw\n");
   if (!Vdp1External.disptoggle)
   {
      Vdp1Regs->regs.EDSR >>= 1;
      Vdp1NoDraw();
   } else {
    if (Vdp1External.status == VDP1_STATUS_IDLE) {
    Vdp1Regs->regs.EDSR >>= 1;
     Vdp1Regs->addr = 0;

     // beginning of a frame
     // BEF <- CEF
     // CEF <- 0
     //Vdp1Regs->regs.EDSR >>= 1;
     /* this should be done after a frame change or a plot trigger */
     Vdp1Regs->regs.COPR = 0;
     Vdp1Regs->lCOPR = 0;
   }
     VIDCore->Vdp1Draw();
     Vdp1DrawCommands(Vdp1Ram, Vdp1Regs, NULL);
   }
   if (Vdp1External.status == VDP1_STATUS_IDLE) {
     FRAMELOG("Vdp1Draw end at %d line\n", yabsys.LineCount);
     Vdp1Regs->regs.EDSR |= 2;
     ScuSendDrawEnd();
   }
   if (Vdp1External.status == VDP1_STATUS_IDLE) return 0;
   else return 1;
}
//////////////////////////////////////////////////////////////////////////////
static void Vdp1TryDraw(void) {
  if ((needVdp1draw == 1)) {
    needVdp1draw = Vdp1Draw();
  }
}
//////////////////////////////////////////////////////////////////////////////
static void Vdp1EraseWrite(int id){
  lastHash = -1;
  if ((VIDCore != NULL) && (VIDCore->Vdp1EraseWrite != NULL))VIDCore->Vdp1EraseWrite(id);
}
//////////////////////////////////////////////////////////////////////////////
static void startField(void) {
  int isrender = 0;
  yabsys.wait_line_count = -1;
  FRAMELOG("StartField ***** VOUT(T) %d FCM=%d FCT=%d VBE=%d PTMR=%d (%d, %d, %d, %d)*****\n", Vdp1External.swap_frame_buffer, (Vdp1Regs->regs.FBCR & 0x02) >> 1, (Vdp1Regs->regs.FBCR & 0x01), (Vdp1Regs->regs.TVMR >> 3) & 0x01, Vdp1Regs->regs.PTMR, Vdp1External.onecyclemode, Vdp1External.manualchange, Vdp1External.manualerase, needVBlankErase());

  // Manual Change
  Vdp1External.swap_frame_buffer |= (Vdp1External.manualchange == 1);
  Vdp1External.swap_frame_buffer |= (Vdp1External.onecyclemode == 1);

  // Frame Change
  if (Vdp1External.swap_frame_buffer == 1)
  {
    addVdp1Framecount();
    FRAMELOG("Swap Line %d\n", yabsys.LineCount);
    lastHash = -1;
    if ((Vdp1External.manualerase == 1) || (Vdp1External.onecyclemode == 1))
    {
      int id = 0;
      if (_Ygl != NULL) id = _Ygl->readframe;
      Vdp1EraseWrite(id);
      Vdp1External.manualerase = 0;
    }

    VIDCore->Vdp1FrameChange();
    FRAMELOG("Change readframe %d to %d (%d)\n", _Ygl->drawframe, _Ygl->readframe, yabsys.LineCount);
    Vdp1External.current_frame = !Vdp1External.current_frame;
    Vdp1Regs->regs.LOPR = Vdp1Regs->regs.COPR;
    Vdp1Regs->regs.COPR = 0;
    Vdp1Regs->lCOPR = 0;
    Vdp1Regs->regs.EDSR >>= 1;

    FRAMELOG("[VDP1] Displayed framebuffer changed. EDSR=%02X", Vdp1Regs->regs.EDSR);

    Vdp1External.swap_frame_buffer = 0;

    // if Plot Trigger mode == 0x02 draw start
    if ((Vdp1Regs->regs.PTMR == 0x2)){
      FRAMELOG("[VDP1] PTMR == 0x2 start drawing immidiatly\n");
      abortVdp1();
      vdp1_clock = 0;
      RequestVdp1ToDraw();
    }
  }
  else {
    if ( Vdp1External.status == VDP1_STATUS_RUNNING) {
      LOG("[VDP1] Start Drawing continue");
      RequestVdp1ToDraw();
    }
  }

  if (Vdp1Regs->regs.PTMR == 0x1) Vdp1External.plot_trigger_done = 0;

  FRAMELOG("End StartField\n");

  Vdp1External.manualchange = 0;
}
//////////////////////////////////////////////////////////////////////////////
static void updateRegisters() {
    if (Vdp1Regs->dirty.TVMR) {
      updateTVMRMode();
      FRAMELOG("TVMR => Write VBE=%d FCM=%d FCT=%d line = %d\n", (Vdp1Regs->regs.TVMR >> 3) & 0x01, (Vdp1Regs->regs.FBCR & 0x02) >> 1, (Vdp1Regs->regs.FBCR & 0x01),  yabsys.LineCount);
    }
    if (Vdp1Regs->dirty.FBCR) {
      FRAMELOG("FBCR => Write VBE=%d FCM=%d FCT=%d line = %d\n", (Vdp1Regs->regs.TVMR >> 3) & 0x01, (Vdp1Regs->regs.FBCR & 0x02) >> 1, (Vdp1Regs->regs.FBCR & 0x01),  yabsys.LineCount);
      updateFBCRMode();
    }
    if (Vdp1Regs->dirty.PTMR) {
      u16 oldPTMR = latchedRegs.regs.PTMR;
      latchedRegs.regs.PTMR = Vdp1Regs->regs.PTMR;
      Vdp1External.plot_trigger_line = -1;
      Vdp1External.plot_trigger_done = 0;
      if (Vdp1Regs->regs.PTMR == 1){
        FRAMELOG("VDP1: VDPEV_DIRECT_DRAW\n");
        Vdp1External.plot_trigger_line = yabsys.LineCount;
        abortVdp1();
        RequestVdp1ToDraw();
        Vdp1TryDraw();
        Vdp1External.plot_trigger_done = 1;
      }
      if ((Vdp1Regs->regs.PTMR == 0x2) && (oldPTMR == 0x0)){
        FRAMELOG("[VDP1] PTMR == 0x2 start drawing immidiatly\n");
        abortVdp1();
        RequestVdp1ToDraw();
        Vdp1TryDraw();
      }
    }
    if (Vdp1Regs->dirty.ENDR) {
      abortVdp1();
    }
    Vdp1Regs->dirtyState = 0;
    memcpy(&latchedRegs.regs, &Vdp1Regs->regs, sizeof(Vdp1_regs));
}
//////////////////////////////////////////////////////////////////////////////
