/* ------------------------------------------------------------------------- */
/*   "tables" :  Constructs the story file (the output) up to the end        */
/*               of dynamic memory, gluing together all the required         */
/*               tables.                                                     */
/*                                                                           */
/*   Part of Inform 6.45                                                     */
/*   copyright (c) Graham Nelson 1993 - 2025                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

uchar *zmachine_paged_memory;          /* Where we shall store the story file
                                          constructed (contains all of paged
                                          memory, i.e. all but code and the
                                          static strings: allocated only when
                                          we know how large it needs to be,
                                          at the end of the compilation pass */

/* In Glulx, zmachine_paged_memory contains all of RAM -- i.e. all but
   the header, the code, the static arrays, and the static strings. */

/* ------------------------------------------------------------------------- */
/*   Offsets of various areas in the Z-machine: these are set to nominal     */
/*   values before the compilation pass, and to their calculated final       */
/*   values only when construct_storyfile() happens.  These are then used to */
/*   backpatch the incorrect values now existing in the Z-machine which      */
/*   used these nominal values.                                              */
/*   Most of the nominal values are 0x800 because this is guaranteed to      */
/*   be assembled as a long constant if it's needed in code, since the       */
/*   largest possible value of scale_factor is 8 and 0x800/8 = 256.          */
/*                                                                           */
/*   In Glulx, I use 0x12345 instead of 0x800. This will always be a long    */
/*   (32-bit) constant, since there's no scale_factor.                       */
/* ------------------------------------------------------------------------- */

int32 code_offset,
      actions_offset,
      preactions_offset,
      dictionary_offset,
      adjectives_offset,
      variables_offset,
      strings_offset,
      class_numbers_offset,
      individuals_offset,
      identifier_names_offset,
      array_names_offset,
      prop_defaults_offset,
      prop_values_offset,
      static_memory_offset,
      attribute_names_offset,
      action_names_offset,
      fake_action_names_offset,
      routine_names_offset,
      constant_names_offset,
      routines_array_offset,
      constants_array_offset,
      routine_flags_array_offset,
      global_names_offset,
      global_flags_array_offset,
      array_flags_array_offset,
      static_arrays_offset;
int32 arrays_offset,
      object_tree_offset,
      grammar_table_offset,
      abbreviations_offset; /* Glulx */

int32 Out_Size, Write_Code_At, Write_Strings_At;
int32 RAM_Size, Write_RAM_At; /* Glulx */

int zcode_compact_globals_adjustment; 

/* ------------------------------------------------------------------------- */
/*   Story file header settings.   (Written to in "directs.c" and "asm.c".)  */
/* ------------------------------------------------------------------------- */

int release_number,                    /* Release number game is to have     */
    statusline_flag;                   /* Either TIME_STYLE or SCORE_STYLE   */

int serial_code_given_in_program       /* If TRUE, a Serial directive has    */
    = FALSE;                           /* specified this 6-digit serial code */
char serial_code_buffer[7];            /* (overriding the usual date-stamp)  */
int flags2_requirements[16];           /* An array of which bits in Flags 2 of
                                          the header will need to be set:
                                          e.g. if the save_undo / restore_undo
                                          opcodes are ever assembled, we have
                                          to set the "games want UNDO" bit.
                                          Values are 0 or 1.                 */

/* ------------------------------------------------------------------------- */
/*   Construct story file (up to code area start).                           */
/*                                                                           */
/*   (To understand what follows, you really need to look at the run-time    */
/*   system's specification, the Z-Machine Standards document.)              */
/* ------------------------------------------------------------------------- */

extern void write_serial_number(char *buffer)
{
    /*  Note that this function may require modification for "ANSI" compilers
        which do not provide the standard time functions: what is needed is
        the ability to work out today's date                                 */

    time_t tt;  tt=time(0);
    if (serial_code_given_in_program) {
        strcpy(buffer, serial_code_buffer);
    }
    else {
#ifdef TIME_UNAVAILABLE
        sprintf(buffer,"970000");
#else
        /* Write a six-digit date, null-terminated. Fall back to "970000"
           if that fails. */
        int len = strftime(buffer,7,"%y%m%d",localtime(&tt));
        if (len != 6)
            sprintf(buffer,"970000");
#endif
    }
}

static char percentage_buffer[64];

static char *show_percentage(int32 x, int32 total)
{
    if (memory_map_setting < 2) {
        percentage_buffer[0] = '\0';
    }
    else if (x == 0) {
        sprintf(percentage_buffer, "  ( --- )");
    }
    else if (memory_map_setting < 3) {
        sprintf(percentage_buffer, "  (%.1f %%)", (float)x * 100.0 / (float)total);
    }
    else {
        sprintf(percentage_buffer, "  (%.1f %%, %d bytes)", (float)x * 100.0 / (float)total, x);
    }
    return percentage_buffer;
}

static char *version_name(int v)
{
    /* Glulx-only */
    return "Glulx";
}

static int32 rough_size_of_paged_memory_g(void)
{
    /*  This function calculates a modest over-estimate of the amount of
        memory required to store the machine's paged memory area
        (that is, everything past the start of RAM). */

    int32 total;

    ASSERT_GLULX();

    /* No header for us! */
    total = 1000; /* bit of a fudge factor */

    total += no_globals * 4; /* global variables */
    total += dynamic_array_area_size; /* arrays */

    total += no_objects * OBJECT_BYTE_LENGTH; /* object tables */
    total += properties_table_size; /* property tables */
    total += no_properties * 4; /* property defaults table */

    total += 4 + no_classes * 4; /* class prototype object numbers */

    total += 32; /* address/length of the identifier tables */
    total += no_properties * 4;
    total += (no_individual_properties-INDIV_PROP_START) * 4;
    total += (NUM_ATTR_BYTES*8) * 4;
    total += (no_actions + no_fake_actions) * 4;
    total += 4 + no_arrays * 4;

    total += 4 + no_Inform_verbs * 4; /* index of grammar tables */
    total += grammar_lines_top; /* grammar tables */

    total += 4 + no_actions * 4; /* actions functions table */

    total += 4;
    total += dictionary_top;

    while (total % GPAGESIZE)
        total++;

    return(total);
}

static void construct_storyfile_g(void)
{   uchar *p;
    int32 i, j, k, l, mark, strings_length;
    int32 globals_at, dictionary_at, actions_at, preactions_at,
          abbrevs_at, prop_defaults_at, object_tree_at, object_props_at,
          grammar_table_at, arrays_at, static_arrays_at;
    int32 threespaces, code_length;
    int32 rough_size;

    ASSERT_GLULX();

    individual_name_strings =
        my_calloc(sizeof(int32), no_individual_properties,
            "identifier name strings");
    action_name_strings =
        my_calloc(sizeof(int32), no_actions + no_fake_actions,
            "action name strings");
    attribute_name_strings =
        my_calloc(sizeof(int32), NUM_ATTR_BYTES*8,
            "attribute name strings");
    array_name_strings =
        my_calloc(sizeof(int32), 
            no_symbols,
            "array name strings");

    write_the_identifier_names();
    threespaces = compile_string("   ", STRCTX_GAME);

    compress_game_text();

    /*  We now know how large the buffer to hold our construction has to be  */

    rough_size = rough_size_of_paged_memory_g();
    zmachine_paged_memory = my_malloc(rough_size, "output buffer");

    /*  Foolish code to make this routine compile on all ANSI compilers      */

    p = (uchar *) zmachine_paged_memory;

    /*  In what follows, the "mark" will move upwards in memory: at various
        points its value will be recorded for milestones like
        "dictionary table start".  It begins at 0x40, just after the header  */

    /* Ok, our policy here will be to set the *_at values all relative
       to RAM. That's so we can write into zmachine_paged_memory[mark] 
       and actually hit what we're aiming at.
       All the *_offset values will be set to true Glulx machine
       addresses. */

    /* To get our bearings, figure out where the strings and code are. */
    /* We start with two words, which conventionally identify the 
       memory layout. This is why the code starts eight bytes after
       the header. */
    Write_Code_At = GLULX_HEADER_SIZE + GLULX_STATIC_ROM_SIZE;
    if (!OMIT_UNUSED_ROUTINES) {
        code_length = zmachine_pc;
    }
    else {
        if ((uint32)zmachine_pc != df_total_size_before_stripping)
            compiler_error("Code size does not match (zmachine_pc and df_total_size).");
        code_length = df_total_size_after_stripping;
    }
    Write_Strings_At = Write_Code_At + code_length;
    strings_length = compression_table_size + compression_string_size;

    static_arrays_at = Write_Strings_At + strings_length;

    /* Now figure out where RAM starts. */
    Write_RAM_At = static_arrays_at + static_array_area_size;
    /* The Write_RAM_At boundary must be a multiple of GPAGESIZE. */
    while (Write_RAM_At % GPAGESIZE)
        Write_RAM_At++;

    /* Now work out all those RAM positions. */
    mark = 0;

    /*  ----------------- Variables and Dynamic Arrays --------------------- */

    globals_at = mark;
    for (i=0; i<no_globals; i++) {
        j = global_initial_value[i].value;
        WriteInt32(p+mark, j);
        mark += 4;
    }

    arrays_at = mark;
    for (i=0; i<dynamic_array_area_size; i++)
        p[mark++] = dynamic_array_area[i];

    /* -------------------------- Dynamic Strings -------------------------- */

    abbrevs_at = mark;
    WriteInt32(p+mark, no_dynamic_strings);
    mark += 4;
    for (i=0; i<no_dynamic_strings; i++) {
        j = Write_Strings_At + compressed_offsets[threespaces-1];
        WriteInt32(p+mark, j);
        mark += 4;
    }

    /*  -------------------- Objects and Properties ------------------------ */

    object_tree_at = mark;

    object_props_at = mark + no_objects*OBJECT_BYTE_LENGTH;

    for (i=0; i<no_objects; i++) {
        int32 objmark = mark;
        p[mark++] = 0x70; /* type byte -- object */
        for (j=0; j<NUM_ATTR_BYTES; j++) {
            p[mark++] = objectatts[i*NUM_ATTR_BYTES+j];
        }
        for (j=0; j<6; j++) {
            int32 val = 0;
            switch (j) {
            case 0: /* next object in the linked list. */
                if (i == no_objects-1)
                    val = 0;
                else
                    val = Write_RAM_At + objmark + OBJECT_BYTE_LENGTH;
                break;
            case 1: /* hardware name address */
                val = Write_Strings_At + compressed_offsets[objectsg[i].shortname-1];
                break;
            case 2: /* property table address */
                val = Write_RAM_At + object_props_at + objectsg[i].propaddr;
                break;
            case 3: /* parent */
                if (objectsg[i].parent == 0)
                    val = 0;
                else
                    val = Write_RAM_At + object_tree_at +
                        (OBJECT_BYTE_LENGTH*(objectsg[i].parent-1));
                break;
            case 4: /* sibling */
                if (objectsg[i].next == 0)
                    val = 0;
                else
                    val = Write_RAM_At + object_tree_at +
                        (OBJECT_BYTE_LENGTH*(objectsg[i].next-1));
                break;
            case 5: /* child */
                if (objectsg[i].child == 0)
                    val = 0;
                else
                    val = Write_RAM_At + object_tree_at +
                        (OBJECT_BYTE_LENGTH*(objectsg[i].child-1));
                break;
            }
            p[mark++] = (val >> 24) & 0xFF;
            p[mark++] = (val >> 16) & 0xFF;
            p[mark++] = (val >> 8) & 0xFF;
            p[mark++] = (val) & 0xFF;
        }

        for (j=0; j<GLULX_OBJECT_EXT_BYTES; j++) {
            p[mark++] = 0;
        }
    }

    if (object_props_at != mark)
        error("*** Object table was impossible length ***");

    for (i=0; i<properties_table_size; i++)
        p[mark+i]=properties_table[i];

    for (i=0; i<no_objects; i++) { 
        int32 tableaddr = object_props_at + objectsg[i].propaddr;
        int32 tablelen = ReadInt32(p+tableaddr);
        tableaddr += 4;
        for (j=0; j<tablelen; j++) {
            k = ReadInt32(p+tableaddr+4);
            k += (Write_RAM_At + object_props_at);
            WriteInt32(p+tableaddr+4, k);
            tableaddr += 10;
        }
    }

    mark += properties_table_size;

    prop_defaults_at = mark;
    for (i=0; i<no_properties; i++) {
        k = commonprops[i].default_value;
        WriteInt32(p+mark, k);
        mark += 4;
    }

    /*  ----------- Table of Class Prototype Object Numbers ---------------- */
    
    class_numbers_offset = mark;
    for (i=0; i<no_classes; i++) {
        j = Write_RAM_At + object_tree_at +
            (OBJECT_BYTE_LENGTH*(class_info[i].object_number-1));
        WriteInt32(p+mark, j);
        mark += 4;
    }
    WriteInt32(p+mark, 0);
    mark += 4;

    /* -------------------- Table of Property Names ------------------------ */

    /* We try to format this bit with some regularity...
       address of common properties
       number of common properties
       address of indiv properties
       number of indiv properties (counted from INDIV_PROP_START)
       address of attributes
       number of attributes (always NUM_ATTR_BYTES*8)
       address of actions
       number of actions
    */

    if (!OMIT_SYMBOL_TABLE) {
        identifier_names_offset = mark;
        mark += 32; /* eight pairs of values, to be filled in. */
  
        WriteInt32(p+identifier_names_offset+0, Write_RAM_At + mark);
        WriteInt32(p+identifier_names_offset+4, no_properties);
        for (i=0; i<no_properties; i++) {
            j = individual_name_strings[i];
            if (j)
                j = Write_Strings_At + compressed_offsets[j-1];
            WriteInt32(p+mark, j);
            mark += 4;
        }
  
        WriteInt32(p+identifier_names_offset+8, Write_RAM_At + mark);
        WriteInt32(p+identifier_names_offset+12, 
                   no_individual_properties-INDIV_PROP_START);
        for (i=INDIV_PROP_START; i<no_individual_properties; i++) {
            j = individual_name_strings[i];
            if (j)
                j = Write_Strings_At + compressed_offsets[j-1];
            WriteInt32(p+mark, j);
            mark += 4;
        }
  
        WriteInt32(p+identifier_names_offset+16, Write_RAM_At + mark);
        WriteInt32(p+identifier_names_offset+20, NUM_ATTR_BYTES*8);
        for (i=0; i<NUM_ATTR_BYTES*8; i++) {
            j = attribute_name_strings[i];
            if (j)
                j = Write_Strings_At + compressed_offsets[j-1];
            WriteInt32(p+mark, j);
            mark += 4;
        }
  
        WriteInt32(p+identifier_names_offset+24, Write_RAM_At + mark);
        WriteInt32(p+identifier_names_offset+28, no_actions + no_fake_actions);
        action_names_offset = mark;
        fake_action_names_offset = mark + 4*no_actions;
        for (i=0; i<no_actions + no_fake_actions; i++) {
            int ax = i;
            if (i<no_actions && GRAMMAR_META_FLAG)
                ax = sorted_actions[i].external_to_int;
            j = action_name_strings[ax];
            if (j)
                j = Write_Strings_At + compressed_offsets[j-1];
            WriteInt32(p+mark, j);
            mark += 4;
        }
  
        array_names_offset = mark;
        WriteInt32(p+mark, no_arrays);
        mark += 4;
        for (i=0; i<no_arrays; i++) {
            j = array_name_strings[i];
            if (j)
                j = Write_Strings_At + compressed_offsets[j-1];
            WriteInt32(p+mark, j);
            mark += 4;
        }
    }
    else {
        identifier_names_offset = mark;
        action_names_offset = mark;
        fake_action_names_offset = mark;
        array_names_offset = mark;
    }

    individuals_offset = mark;

    /*  ------------------------ Grammar Table ----------------------------- */

    grammar_table_at = mark;

    WriteInt32(p+mark, no_Inform_verbs);
    mark += 4;

    mark += no_Inform_verbs*4;

    for (i=0; i<no_Inform_verbs; i++) {
        j = mark + Write_RAM_At;
        WriteInt32(p+(grammar_table_at+4+i*4), j);
        if (!Inform_verbs[i].used) {
            /* This verb was marked unused at locate_dead_grammar_lines()
               time. Omit the grammar lines. */
            p[mark++] = 0;
            continue;
        }
        p[mark++] = Inform_verbs[i].lines;
        for (j=0; j<Inform_verbs[i].lines; j++) {
            int tok;
            k = Inform_verbs[i].l[j];
            p[mark++] = grammar_lines[k++];
            p[mark++] = grammar_lines[k++];
            p[mark++] = grammar_lines[k++];
            for (;;) {
                tok = grammar_lines[k++];
                p[mark++] = tok;
                if (tok == 15) break;
                p[mark++] = grammar_lines[k++];
                p[mark++] = grammar_lines[k++];
                p[mark++] = grammar_lines[k++];
                p[mark++] = grammar_lines[k++];
            }
        }
    }

    /*  ------------------- Actions and Preactions ------------------------- */

    actions_at = mark;
    WriteInt32(p+mark, no_actions);
    mark += 4;
    mark += no_actions*4;
    /* Values to be written in later. */

    if (DICT_CHAR_SIZE != 1) {
        /* If the dictionary is Unicode, we'd like it to be word-aligned. */
        while (mark % 4)
            p[mark++]=0;
    }

    preactions_at = mark;
    adjectives_offset = mark;
    dictionary_offset = mark;

    /*  ------------------------- Dictionary ------------------------------- */

    dictionary_at = mark;

    WriteInt32(dictionary+0, dict_entries);
    for (i=0; i<4; i++) 
        p[mark+i] = dictionary[i];

    for (i=0; i<dict_entries; i++) {
        k = 4 + i*DICT_ENTRY_BYTE_LENGTH;
        j = mark + 4 + final_dict_order[i]*DICT_ENTRY_BYTE_LENGTH;
        for (l=0; l<DICT_ENTRY_BYTE_LENGTH; l++)
            p[j++] = dictionary[k++];
    }
    mark += 4 + dict_entries * DICT_ENTRY_BYTE_LENGTH;

    /*  -------------------------- All Data -------------------------------- */
    
    /* The end-of-RAM boundary must be a multiple of GPAGESIZE. */
    while (mark % GPAGESIZE)
        p[mark++]=0;

    RAM_Size = mark;

    if (RAM_Size > rough_size)
        compiler_error("RAM size exceeds rough estimate.");
    
    Out_Size = Write_RAM_At + RAM_Size;

    /*  --------------------------- Offsets -------------------------------- */

    dictionary_offset = Write_RAM_At + dictionary_at;
    variables_offset = Write_RAM_At + globals_at;
    arrays_offset = Write_RAM_At + arrays_at;
    actions_offset = Write_RAM_At + actions_at;
    preactions_offset = Write_RAM_At + preactions_at;
    prop_defaults_offset = Write_RAM_At + prop_defaults_at;
    object_tree_offset = Write_RAM_At + object_tree_at;
    prop_values_offset = Write_RAM_At + object_props_at;
    static_memory_offset = Write_RAM_At + grammar_table_at;
    grammar_table_offset = Write_RAM_At + grammar_table_at;
    abbreviations_offset = Write_RAM_At + abbrevs_at;

    code_offset = Write_Code_At;
    strings_offset = Write_Strings_At;
    static_arrays_offset = static_arrays_at;

    /*  --------------------------- The Header ----------------------------- */

    /*  ------ Backpatch the machine, now that all information is in ------- */

    if (TRUE)
        {   backpatch_zmachine_image_g();

            /* The action and grammar tables must be backpatched specially. */
        
            mark = actions_at + 4;
            for (i=0; i<no_actions; i++) {
                int ax = i;
                if (GRAMMAR_META_FLAG)
                    ax = sorted_actions[i].external_to_int;
                j = actions[ax].byte_offset;
                if (OMIT_UNUSED_ROUTINES)
                    j = df_stripped_address_for_address(j);
                j += code_offset;
                WriteInt32(p+mark, j);
                mark += 4;
            }

            for (l = 0; l<no_Inform_verbs; l++) {
                int linecount;
                k = grammar_table_at + 4 + 4*l; 
                i = ((p[k] << 24) | (p[k+1] << 16) | (p[k+2] << 8) | (p[k+3]));
                i -= Write_RAM_At;
                linecount = p[i++];
                for (j=0; j<linecount; j++) {
                    if (GRAMMAR_META_FLAG) {
                        /* backpatch the action number */
                        int action = (p[i+0] << 8) | (p[i+1]);
                        action = sorted_actions[action].internal_to_ext;
                        p[i+0] = (action >> 8) & 0xFF;
                        p[i+1] = (action & 0xFF);
                    }
                    i = i + 3;
                    while (p[i] != 15) {
                        int topbits = (p[i]/0x40) & 3;
                        int32 value = ((p[i+1] << 24) | (p[i+2] << 16) 
                                       | (p[i+3] << 8) | (p[i+4]));
                        switch(topbits) {
                        case 1:
                            value = dictionary_offset + 4
                                + final_dict_order[value]*DICT_ENTRY_BYTE_LENGTH;
                            break;
                        case 2:
                            if (OMIT_UNUSED_ROUTINES)
                                value = df_stripped_address_for_address(value);
                            value += code_offset;
                            break;
                        }
                        WriteInt32(p+(i+1), value);
                        i = i + 5;
                    }
                    i++;
                }
            }

        }

    /*  ---- From here on, it's all reportage: construction is finished ---- */

    if (debugfile_switch)
    {
        write_debug_information_for_actions();
        
        begin_writing_debug_sections();
        write_debug_section("memory layout id", GLULX_HEADER_SIZE);
        write_debug_section("code area", Write_Code_At);
        write_debug_section("string decoding table", Write_Strings_At);
        write_debug_section("strings area",
                            Write_Strings_At + compression_table_size);
        write_debug_section("static array space", static_arrays_at);
        if (static_arrays_at + static_array_area_size < Write_RAM_At)
        {   write_debug_section
                ("zero padding", static_arrays_at + static_array_area_size);
        }
        if (globals_at)
        {   compiler_error("Failed assumption that globals are at start of "
                           "Glulx RAM");
        }
        write_debug_section("global variables", Write_RAM_At + globals_at);
        write_debug_section("array space", Write_RAM_At + arrays_at);
        write_debug_section("abbreviations table", Write_RAM_At + abbrevs_at);
        write_debug_section("object tree", Write_RAM_At + object_tree_at);
        write_debug_section("common properties",
                            Write_RAM_At + object_props_at);
        write_debug_section("property defaults",
                            Write_RAM_At + prop_defaults_at);
        write_debug_section("class numbers",
                            Write_RAM_At + class_numbers_offset);
        write_debug_section("identifier names",
                            Write_RAM_At + identifier_names_offset);
        write_debug_section("grammar table", Write_RAM_At + grammar_table_at);
        write_debug_section("actions table", Write_RAM_At + actions_at);
        write_debug_section("dictionary", Write_RAM_At + dictionary_at);
        if (MEMORY_MAP_EXTENSION)
        {   write_debug_section("zero padding", Out_Size);
        }
        end_writing_debug_sections(Out_Size + MEMORY_MAP_EXTENSION);
    }

    if (memory_map_setting)
    {
        int32 addr;
        {
printf("        +---------------------+   000000\n");
printf("Read-   |       header        |   %s\n",
    show_percentage(GLULX_HEADER_SIZE, Out_Size));
printf(" only   +=====================+   %06lx\n", (long int) GLULX_HEADER_SIZE);
printf("memory  |  memory layout id   |   %s\n",
    show_percentage(Write_Code_At-GLULX_HEADER_SIZE, Out_Size));
printf("        +---------------------+   %06lx\n", (long int) Write_Code_At);
printf("        |        code         |   %s\n",
    show_percentage(Write_Strings_At-Write_Code_At, Out_Size));
printf("        +---------------------+   %06lx\n",
    (long int) Write_Strings_At);
printf("        | string decode table |   %s\n",
    show_percentage(compression_table_size, Out_Size));
printf("        + - - - - - - - - - - +   %06lx\n",
    (long int) Write_Strings_At + compression_table_size);
addr = (static_array_area_size ? static_arrays_at : Write_RAM_At+globals_at);
printf("        |       strings       |   %s\n",
    show_percentage(addr-(Write_Strings_At + compression_table_size), Out_Size));
            if (static_array_area_size)
            {
printf("        +---------------------+   %06lx\n", 
    (long int) (static_arrays_at));
printf("        |    static arrays    |   %s\n",
    show_percentage(Write_RAM_At+globals_at-static_arrays_at, Out_Size));
            }
printf("        +=====================+   %06lx\n", 
    (long int) (Write_RAM_At+globals_at));
printf("Dynamic |  global variables   |   %s\n",
    show_percentage(arrays_at-globals_at, Out_Size));
printf("memory  + - - - - - - - - - - +   %06lx\n",
    (long int) (Write_RAM_At+arrays_at));
printf("        |       arrays        |   %s\n",
    show_percentage(abbrevs_at-arrays_at, Out_Size));
printf("        +---------------------+   %06lx\n",
    (long int) (Write_RAM_At+abbrevs_at));
printf("        | printing variables  |   %s\n",
    show_percentage(object_tree_at-abbrevs_at, Out_Size));
printf("        +---------------------+   %06lx\n", 
    (long int) (Write_RAM_At+object_tree_at));
printf("        |       objects       |   %s\n",
    show_percentage(object_props_at-object_tree_at, Out_Size));
printf("        + - - - - - - - - - - +   %06lx\n",
    (long int) (Write_RAM_At+object_props_at));
printf("        |   property values   |   %s\n",
    show_percentage(prop_defaults_at-object_props_at, Out_Size));
printf("        + - - - - - - - - - - +   %06lx\n",
    (long int) (Write_RAM_At+prop_defaults_at));
printf("        |  property defaults  |   %s\n",
    show_percentage(class_numbers_offset-prop_defaults_at, Out_Size));
printf("        + - - - - - - - - - - +   %06lx\n",
    (long int) (Write_RAM_At+class_numbers_offset));
printf("        | class numbers table |   %s\n",
    show_percentage(identifier_names_offset-class_numbers_offset, Out_Size));
printf("        + - - - - - - - - - - +   %06lx\n",
    (long int) (Write_RAM_At+identifier_names_offset));
printf("        |   id names table    |   %s\n",
    show_percentage(grammar_table_at-identifier_names_offset, Out_Size));
printf("        +---------------------+   %06lx\n",
    (long int) (Write_RAM_At+grammar_table_at));
printf("        |    grammar table    |   %s\n",
    show_percentage(actions_at-grammar_table_at, Out_Size));
printf("        + - - - - - - - - - - +   %06lx\n", 
    (long int) (Write_RAM_At+actions_at));
printf("        |       actions       |   %s\n",
    show_percentage(dictionary_offset-(Write_RAM_At+actions_at), Out_Size));
printf("        +---------------------+   %06lx\n", 
    (long int) dictionary_offset);
printf("        |     dictionary      |   %s\n",
    show_percentage(Out_Size-dictionary_offset, Out_Size));
            if (MEMORY_MAP_EXTENSION == 0)
            {
printf("        +---------------------+   %06lx\n", (long int) Out_Size);
            }
            else
            {
printf("        +=====================+   %06lx\n", (long int) Out_Size);
printf("Runtime |       (empty)       |\n");   /* no percentage */
printf("  extn  +---------------------+   %06lx\n", (long int) Out_Size+MEMORY_MAP_EXTENSION);
            }

        }

    }
}

static void display_frequencies()
{
    int i, j;
    
    printf("How frequently abbreviations were used, and roughly\n");
    printf("how many bytes they saved:  ('_' denotes spaces)\n");
    
    for (i=0; i<no_abbreviations; i++) {
        int32 saving;
        char *astr;
        /* Glulx-only saving calculation */
        saving = (abbreviations[i].freq-1)*abbreviations[i].quality;

        astr = abbreviation_text(i);
        /* Print the abbreviation text, left-padded to ten spaces, with
           spaces replaced by underscores. */
        for (j=strlen(astr); j<10; j++) {
            putchar(' ');
        }
        for (j=0; astr[j]; j++) {
            putchar(astr[j] == ' ' ? '_' : astr[j]);
        }
        
        printf(" %5d/%5d   ", abbreviations[i].freq, saving);
        
        if ((i%3)==2) printf("\n");
    }
    if ((i%3)!=0) printf("\n");
    
    if (no_abbreviations==0) printf("None were declared.\n");
}

static void display_statistics_g()
{
    int32 k_long, rate;
    char *k_str = "";
    int32 limit = 1024*1024;
    int32 strings_length = compression_table_size + compression_string_size;
    char *output_called = "story file";
    
    k_long=(Out_Size/1024);
    if ((Out_Size-1024*k_long) >= 512) { k_long++; k_str=""; }
    else if ((Out_Size-1024*k_long) > 0) { k_str=".5"; }
    
    if (strings_length == 0) rate = 0;
    else rate=strings_length*1000/total_chars_trans;

    {   printf("In:\
%3d source code files            %6d syntactic lines\n\
%6d textual lines              %8ld characters ",
               total_input_files, no_syntax_lines,
               total_source_line_count, (long int) total_chars_read);
        if (character_set_unicode) printf("(UTF-8)\n");
        else if (character_set_setting == 0) printf("(plain ASCII)\n");
        else
            {   printf("(ISO 8859-%d %s)\n", character_set_setting,
                       name_of_iso_set(character_set_setting));
            }

        {char serialnum[8];
            write_serial_number(serialnum);
            printf("Allocated:\n\
%6d symbols                    %8ld bytes of memory\n\
Out:   %s %s %d.%c%c%c%c%c%c (%ld%sK long):\n",
                   no_symbols,
                   (long int) malloced_bytes,
                   version_name(version_number),
                   output_called,
                   release_number,
                   serialnum[0], serialnum[1], serialnum[2],
                   serialnum[3], serialnum[4], serialnum[5],
                   (long int) k_long, k_str);
        } 

        printf("\
%6d classes                      %6d objects\n\
%6d global vars                  %6d variable/array space\n",
               no_classes,
               no_objects,
               no_globals,
               dynamic_array_area_size);

        printf(
               "%6d verbs                        %6d dictionary entries\n\
%6d grammar lines (version %d)    %6d grammar tokens (unlimited)\n\
%6d actions                      %6d attributes (maximum %2d)\n\
%6d common props (maximum %3d)   %6d individual props (unlimited)\n",
               no_Inform_verbs,
               dict_entries,
               no_grammar_lines, grammar_version_number,
               no_grammar_tokens,
               no_actions,
               no_attributes, NUM_ATTR_BYTES*8,
               no_properties-3, INDIV_PROP_START-3,
               no_individual_properties - INDIV_PROP_START);

        if (track_unused_routines)
            {
                uint32 diff = df_total_size_before_stripping - df_total_size_after_stripping;
                printf(
                       "%6ld bytes of code                %6ld unused bytes %s (%.1f%%)\n",
                       (long int) df_total_size_before_stripping, (long int) diff,
                       (OMIT_UNUSED_ROUTINES ? "stripped out" : "detected"),
                       100 * (float)diff / (float)df_total_size_before_stripping);
            }

        printf(
               "%6ld characters used in text      %6ld bytes compressed (rate %d.%3ld)\n\
%6d abbreviations (maximum %d)   %6d routines (unlimited)\n\
%6ld instructions of code         %6d sequence points\n\
%6ld bytes writable memory used   %6ld bytes read-only memory used\n\
%6ld bytes used in machine    %10ld bytes free in machine\n",
               (long int) total_chars_trans,
               (long int) strings_length,
               (total_chars_trans>strings_length)?0:1,
               (long int) rate,
               no_abbreviations, MAX_ABBREVS,
               no_routines,
               (long int) no_instructions, no_sequence_points,
               (long int) (Out_Size - Write_RAM_At),
               (long int) Write_RAM_At,
               (long int) Out_Size,
               (long int)
               (((long int) (limit*1024L)) - ((long int) Out_Size)));

    }
}


extern void construct_storyfile(void)
{
    /* Glulx-only: construct Glulx storyfile */
    construct_storyfile_g();

    /* Display all the trace/stats info that came out of compilation.

       (Except for the memory map, which uses a bunch of local variables
       from construct_storyfile_z/g(), so it's easier to do that inside
       that function.)
    */
    
    if (frequencies_setting)
        display_frequencies();

    if (list_symbols_setting)
        list_symbols(list_symbols_setting);
    
    if (list_dict_setting)
        show_dictionary(list_dict_setting);
    
    if (list_verbs_setting)
        list_verb_table();

    if (printactions_switch)
        list_action_table();

    if (list_objects_setting)
        list_object_tree();
    
    if (statistics_switch) {
        /* Glulx-only statistics */
        display_statistics_g();
    }
}

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_tables_vars(void)
{
    release_number = 1;
    statusline_flag = SCORE_STYLE;

    zmachine_paged_memory = NULL;

    /* Glulx-only default offsets */
    code_offset = 0x12345;
    actions_offset = 0x12345;
    preactions_offset = 0x12345;
    dictionary_offset = 0x12345;
    adjectives_offset = 0x12345;
    variables_offset = 0x12345;
    arrays_offset = 0x12345;
    strings_offset = 0x12345;
    individuals_offset = 0x12345;
    identifier_names_offset = 0x12345;
    class_numbers_offset = 0x12345;
    static_arrays_offset = 0x12345;
    zcode_compact_globals_adjustment = -1;
}

extern void tables_begin_pass(void)
{
}

extern void tables_allocate_arrays(void)
{
}

extern void tables_free_arrays(void)
{
    /*  Allocation for this array happens in construct_storyfile() above     */

    my_free(&zmachine_paged_memory,"output buffer");
}

/* ========================================================================= */
