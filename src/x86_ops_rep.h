static int opREPNE(uint32_t fetchdat)
{
        return rep386(0);
}
static int opREPE(uint32_t fetchdat)
{       
        return rep386(1);
}

