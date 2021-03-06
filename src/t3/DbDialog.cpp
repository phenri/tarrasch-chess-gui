/****************************************************************************
 * Custom dialog - Database browser
 *  Author:  Bill Forster
 *  License: MIT license. Full text of license is in associated file LICENSE
 *  Copyright 2010-2014, Bill Forster <billforsternz at gmail dot com>
 ****************************************************************************/
#define _CRT_SECURE_NO_DEPRECATE
#include "wx/wx.h"
#include "wx/valtext.h"
#include "wx/valgen.h"
#include "wx/listctrl.h"
#include "wx/notebook.h"
#include "Portability.h"
#ifdef THC_MAC
#include <sys/time.h>               // for gettimeofday()
#endif
#ifdef THC_WINDOWS
#include <windows.h>                // for QueryPerformanceCounter()
#endif
#include "Appdefs.h"
#include "DebugPrintf.h"
#include "thc.h"
#include "GameDetailsDialog.h"
#include "GamePrefixDialog.h"
#include "GameLogic.h"
#include "Objects.h"
#include "PgnFiles.h"
#include "MiniBoard.h"
#include "DbDialog.h"
#include "Database.h"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
using namespace std;

// DbDialog type definition
IMPLEMENT_CLASS( DbDialog, wxDialog )

// DbDialog event table definition
BEGIN_EVENT_TABLE( DbDialog, wxDialog )
//  EVT_CLOSE( DbDialog::OnClose )
    EVT_ACTIVATE(DbDialog::OnActivate)
    EVT_BUTTON( wxID_OK,                DbDialog::OnOkClick )
    EVT_BUTTON( ID_DB_UTILITY,          DbDialog::OnUtility )
    EVT_BUTTON( ID_DB_RELOAD,           DbDialog::OnReload )
    EVT_BUTTON( wxID_CANCEL,            DbDialog::OnCancel )
    EVT_BUTTON( ID_SAVE_ALL_TO_A_FILE,  DbDialog::OnSaveAllToAFile )
/*  EVT_BUTTON( ID_BOARD2GAME,          DbDialog::OnBoard2Game )
    EVT_CHECKBOX( ID_REORDER,           DbDialog::OnRenumber )
    EVT_BUTTON( ID_ADD_TO_CLIPBOARD,    DbDialog::OnAddToClipboard )
    EVT_BUTTON( ID_PGN_DIALOG_GAME_DETAILS,   DbDialog::OnEditGameDetails )
    EVT_BUTTON( ID_PGN_DIALOG_GAME_PREFIX,    DbDialog::OnEditGamePrefix )
    EVT_BUTTON( ID_PGN_DIALOG_PUBLISH,  DbDialog::OnPublish )
    EVT_BUTTON( wxID_COPY,              DbDialog::OnCopy )
    EVT_BUTTON( wxID_CUT,               DbDialog::OnCut )
    EVT_BUTTON( wxID_DELETE,            DbDialog::OnDelete )
    EVT_BUTTON( wxID_PASTE,             DbDialog::OnPaste )
    EVT_BUTTON( wxID_SAVE,              DbDialog::OnSave ) */
    EVT_BUTTON( wxID_HELP,              DbDialog::OnHelpClick )

    EVT_RADIOBUTTON( ID_DB_RADIO,       DbDialog::OnRadio )
    EVT_CHECKBOX   ( ID_DB_CHECKBOX,    DbDialog::OnCheckBox )
    EVT_COMBOBOX   ( ID_DB_COMBO,       DbDialog::OnComboBox )
    EVT_LISTBOX(ID_DB_LISTBOX_STATS, DbDialog::OnNextMove)

    //EVT_MENU( wxID_SELECTALL, DbDialog::OnSelectAll )
    EVT_LIST_ITEM_FOCUSED(ID_PGN_LISTBOX, DbDialog::OnListFocused)
    EVT_LIST_ITEM_ACTIVATED(ID_PGN_LISTBOX, DbDialog::OnListSelected)
    EVT_LIST_COL_CLICK(ID_PGN_LISTBOX, DbDialog::OnListColClick)
    EVT_NOTEBOOK_PAGE_CHANGED( wxID_ANY, DbDialog::OnTabSelected)
END_EVENT_TABLE()

// It's a pity we have these static vars, but unfortunately OnGetItemText() must be const for some reason
static thc::ChessPosition gbl_updated_position;
static int gbl_last_item;
static DB_GAME_INFO gbl_info;
static std::vector< thc::Move > gbl_focus_moves;

int focus_idx;
int focus_offset;
MiniBoard *mini_board;

// Read game information from memory if available
bool DbDialog::ReadItemFromMemory( int item )
{
    bool in_memory = false;
    gbl_info.transpo_nbr = 0;
    if( games.size() > item )
    {
        in_memory = true;
        gbl_info = games[item];
        gbl_info.transpo_nbr = 0;
        cprintf( "ReadItemFromMemory(%d), white=%s\n", item, gbl_info.white.c_str() );
        if( gbl_info.move_txt.length() == 0 )
        {
            db_calculate_move_txt(&gbl_info);
            if( transpo_activated && transpositions.size() > 1 )
            {
                for( unsigned int j=0; j<transpositions.size(); j++ )
                {
                    std::string &this_one = transpositions[j].blob;
                    const char *p = this_one.c_str();
                    size_t len = this_one.length();
                    if( gbl_info.str_blob.length()>=len && 0 == memcmp(p,gbl_info.str_blob.c_str(),len) )
                    {
                        gbl_info.transpo_nbr = j+1;
                        break;
                    }
                }
            }
        }
    }
    return in_memory;
}

class wxVirtualListCtrl: public wxListCtrl
{
    //DECLARE_CLASS( wxVirtualListCtrl )
    DECLARE_EVENT_TABLE()
public:
    wxVirtualListCtrl( wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style )
    : wxListCtrl( parent, id, pos, size, wxLC_REPORT|wxLC_VIRTUAL )
    {
        focus_idx = 0;
        focus_offset = 0;
        gbl_last_item = -1;
    }
    //~wxVirtualListCtrl();
    void OnChar( wxKeyEvent &event );

public:
    DbDialog *data_src;
    int focus_idx;
    int focus_offset;
    int initial_focus_offset;
    MiniBoard *mini_board;

    // Read game information from games or database
    void ReadItem( int item ) const
    {
        bool in_memory = data_src->ReadItemFromMemory( item );
        if( !in_memory )
        {
            if( item != gbl_last_item )
            {
                //cprintf( "ListCtrl::ReadItem(%d) READ FROM DATABASE REQUIRED\n", item );
                objs.db->GetRow( &gbl_info, item );
                gbl_info.transpo_nbr = 0;
                gbl_last_item = item;
            }
        }
    }
    
    // Focus changes to new item;
    void ReceiveFocus( int focus_idx )
    {
        cprintf( "ListCtrl::ReceiveFocus(%d)\n", focus_idx );
        this->focus_idx = focus_idx;
        ReadItem(focus_idx);
        char buf[1000];
        char bufw[1000];
        char bufb[1000];
        strcpy( bufw, gbl_info.white.c_str() );
        bufw[19] = '\0';
        strcpy( bufb, gbl_info.black.c_str() );
        bufb[19] = '\0';
        sprintf( buf, "%s - %s", bufw, bufb );
        initial_focus_offset = focus_offset = db_calculate_move_vector( &gbl_info, gbl_focus_moves );
        if( mini_board )
        {
            CalculateMoveTxt();
            //cprintf( "ReceiveFocus(): SetPosition() %s\n", gbl_updated_position.ToDebugStr().c_str()  );
            mini_board->SetPosition( gbl_updated_position.squares );
            cprintf( "Setting board label(%d): %s\n", focus_idx, buf );
            data_src->player_names->SetLabel(wxString(buf));
        }
    }

    std::string CalculateMoveTxt() const
    {
        std::string move_txt;
        thc::ChessRules cr;
        bool first=true;
        for( size_t i=0; i<gbl_focus_moves.size(); i++ )
        {
            thc::Move mv = gbl_focus_moves[i];
            if( i >= focus_offset )
            {
                std::string s = mv.NaturalOut(&cr);
                if( i%2 == 0 || first )
                {
                    if( first )
                        gbl_updated_position = cr;
                    first = false;
                    char buf[100];
                    sprintf( buf, "%lu%s", i/2+1, i%2==0?".":"..." );
                    move_txt += buf;
                }
                move_txt += s;
                move_txt += " ";
                if( i+1 == gbl_focus_moves.size() )
                    move_txt += gbl_info.result;
                else if( i < gbl_focus_moves.size()-5 && move_txt.length()>100 )
                {
                    move_txt += "...";  // very long lines get over truncated by the list control (sad but true)
                    break;
                }
            }
            cr.PlayMove(mv);
        }
        if( first )
        {
            gbl_updated_position = cr;
            move_txt = gbl_info.result;
        }
        //cprintf( "CalculateMoveTxt(): [%s]%s\n", move_txt.c_str(), gbl_updated_position.ToDebugStr().c_str() );
        return move_txt;
    }
    
protected:
    virtual wxString OnGetItemText( long item, long column) const
    {
        //cprintf( "ListCtrl::OnGetItemText(%ld,%ld)\n", item, column );
        std::string move_txt;
        const char *txt;
        ReadItem(item);
        switch( column )
        {
            default: txt =  "";                       break;
            case 1: txt =   gbl_info.white.c_str();       break;
            case 3: txt =   gbl_info.black.c_str();       break;
            case 8: txt =   gbl_info.result.c_str();      break;
            case 10:
            {
                char buf[1000];
                buf[0] = '\0';
                if( gbl_info.transpo_nbr > 0 )
                    sprintf(buf,"(T%d) ", gbl_info.transpo_nbr );
                if( item == focus_idx )
                {
                    move_txt = CalculateMoveTxt();
                    if( focus_offset == initial_focus_offset )
                        move_txt = buf + move_txt;
                }
                else
                {
                    move_txt = gbl_info.move_txt.c_str();
                    move_txt = buf + move_txt;
                }
                txt = move_txt.c_str();
                break;
            }
        }
        wxString ws(txt);
        return ws;
    }
};


// DbDialog event table definition
BEGIN_EVENT_TABLE( wxVirtualListCtrl, wxListCtrl )
    EVT_CHAR(wxVirtualListCtrl::OnChar)
END_EVENT_TABLE()

void wxVirtualListCtrl::OnChar( wxKeyEvent &event )
{
    //cprintf( "OnChar" );
    switch ( event.GetKeyCode() )
    {
        case WXK_LEFT:
            //cprintf( "WXK_LEFT\n" );
            if( focus_offset > 0 )
                focus_offset--;
            RefreshItem(focus_idx);
            if( mini_board )
            {
                CalculateMoveTxt();
                //cprintf( "WXK_LEFT: SetPosition() %s\n", gbl_updated_position.ToDebugStr().c_str()  );
                mini_board->SetPosition( gbl_updated_position.squares );
            }
            break;
        case WXK_RIGHT:
            //cprintf( "WXK_RIGHT\n" );
            if( focus_offset < gbl_focus_moves.size() )
                focus_offset++;
            RefreshItem(focus_idx);
            if( mini_board )
            {
                CalculateMoveTxt();
                //cprintf( "WXK_RIGHT: SetPosition() %s\n", gbl_updated_position.ToDebugStr().c_str()  );
                mini_board->SetPosition( gbl_updated_position.squares );
            }
            break;
        default:
            event.Skip();
    }
}

// DbDialog constructors
DbDialog::DbDialog
(
    wxWindow* parent,
    thc::ChessRules *cr,
    GamesCache  *gc,
    GamesCache  *gc_clipboard,
    wxWindowID id,
    const wxPoint& pos, const wxSize& size, long style
)
{
    this->cr = *cr;
    this->gc = gc;
    this->gc_clipboard = gc_clipboard;
    this->id = id;
    file_game_idx = -1;
    Init();
    Create( parent, id, "Title FIXME", pos, size, style );
}

// Pre window creation initialisation
void DbDialog::Init()
{
    list_ctrl_stats = NULL;
    list_ctrl = NULL;
    selected_game = NULL;
    db_game_set = false;
    activated_at_least_once = false;
    transpo_activated = false;
    wxAcceleratorEntry entries[5];
    entries[0].Set(wxACCEL_CTRL,  (int) 'X',     wxID_CUT);
    entries[1].Set(wxACCEL_CTRL,  (int) 'C',     wxID_COPY);
    entries[2].Set(wxACCEL_CTRL,  (int) 'V',     wxID_PASTE);
    entries[3].Set(wxACCEL_CTRL,  (int) 'A',     wxID_SELECTALL);
    entries[4].Set(wxACCEL_NORMAL,  WXK_DELETE,  wxID_DELETE);
    wxAcceleratorTable accel(5, entries);
    SetAcceleratorTable(accel);
}

// Window creation
bool DbDialog::Create( wxWindow* parent,
  wxWindowID id, const wxString& caption,
  const wxPoint& pos, const wxSize& size, long style )
{
    bool okay=true;

    // We have to set extra styles before creating the dialog
    SetExtraStyle( wxWS_EX_BLOCK_EVENTS|wxDIALOG_EX_CONTEXTHELP );
    if( !wxDialog::Create( parent, id, caption, pos, size, style ) )
        okay = false;
    else
    {

        CreateControls();
        SetDialogHelp();
        SetDialogValidators();

        // This fits the dialog to the minimum size dictated by the sizers
        GetSizer()->Fit(this);
        
        // This ensures that the dialog cannot be sized smaller than the minimum size
        GetSizer()->SetSizeHints(this);

        // Centre the dialog on the parent or (if none) screen
        Centre();
    }
    return okay;
}

static int gbl_nbr; //FIXME

// Control creation for DbDialog
void DbDialog::CreateControls()
{    

    // A top-level sizer
    wxBoxSizer* top_sizer = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(top_sizer);
    
    // A second box sizer to give more space around the controls
    wxBoxSizer* box_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(box_sizer, 0, wxALIGN_CENTER_HORIZONTAL|wxALL, 5);

    // A friendly message
    gbl_nbr = objs.db->SetPosition( cr ); //gc->gds.size();
    char buf[200];
    sprintf(buf,"List of %d matching games from the database",gbl_nbr);
    title_ctrl = new wxStaticText( this, wxID_STATIC,
        buf, wxDefaultPosition, wxDefaultSize, 0 );
    box_sizer->Add(title_ctrl, 0, wxALIGN_LEFT|wxALL, 5);

    // Spacer
    box_sizer->Add(5, 5, 0, wxALIGN_CENTER_HORIZONTAL|wxALL, 5);
    int disp_width, disp_height;
    wxDisplaySize(&disp_width, &disp_height);
    wxSize sz;
    if( disp_width > 1366 )
        disp_width = 1366;
    sz.x = (disp_width*4)/5;
    sz.y = (disp_height*1)/2;
    list_ctrl  = new wxVirtualListCtrl( this, ID_PGN_LISTBOX, wxDefaultPosition, sz/*wxDefaultSize*/,wxLC_REPORT|wxLC_VIRTUAL );
    list_ctrl->data_src = this;
    list_ctrl->SetItemCount(gbl_nbr);
    list_ctrl->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

    list_ctrl->InsertColumn( 0, id==ID_PGN_DIALOG_FILE?"#":" "  );
    list_ctrl->InsertColumn( 1, "White"    );
    list_ctrl->InsertColumn( 2, "Elo W"    );
    list_ctrl->InsertColumn( 3, "Black"    );
    list_ctrl->InsertColumn( 4, "Elo B"    );
    list_ctrl->InsertColumn( 5, "Date"     );
    list_ctrl->InsertColumn( 6, "Site"     );
    list_ctrl->InsertColumn( 7, "Round"    );
    list_ctrl->InsertColumn( 8, "Result"   );
    list_ctrl->InsertColumn( 9, "ECO"      );
    list_ctrl->InsertColumn(10, "Moves"    );
    int col_flag=0;
    int cols[11];

    // Only use the non volatile column widths if they validate okay
    if( objs.repository->nv.m_col0 > 0 &&
        objs.repository->nv.m_col1 > 0 &&
        objs.repository->nv.m_col2 > 0 &&
        objs.repository->nv.m_col3 > 0 &&
        objs.repository->nv.m_col4 > 0 &&
        objs.repository->nv.m_col5 > 0 &&
        objs.repository->nv.m_col6 > 0 &&
        objs.repository->nv.m_col7 > 0 &&
        objs.repository->nv.m_col8 > 0 &&
        objs.repository->nv.m_col9 > 0 &&
        objs.repository->nv.m_col10 > 0
      )
    {
        cols[0] = objs.repository->nv.m_col0;
        cols[1] = objs.repository->nv.m_col1;
        cols[2] = objs.repository->nv.m_col2;
        cols[3] = objs.repository->nv.m_col3;
        cols[4] = objs.repository->nv.m_col4;
        cols[5] = objs.repository->nv.m_col5;
        cols[6] = objs.repository->nv.m_col6;
        cols[7] = objs.repository->nv.m_col7;
        cols[8] = objs.repository->nv.m_col8;
        cols[9] = objs.repository->nv.m_col9;
        cols[10]= objs.repository->nv.m_col10;
    }
    else // else set some sensible defaults
    {
        int x   = (sz.x*98)/100;
        objs.repository->nv.m_col0 = cols[0] =   4*x/97;    // "Game #"
        objs.repository->nv.m_col1 = cols[1] =  14*x/97;    // "White" 
        objs.repository->nv.m_col2 = cols[2] =   6*x/97;    // "Elo W"
        objs.repository->nv.m_col3 = cols[3] =  14*x/97;    // "Black" 
        objs.repository->nv.m_col4 = cols[4] =   6*x/97;    // "Elo B" 
        objs.repository->nv.m_col5 = cols[5] =  10*x/97;    // "Date"  
        objs.repository->nv.m_col6 = cols[6] =   9*x/97;    // "Site"  
        objs.repository->nv.m_col7 = cols[7] =   7*x/97;    // "Round" 
        objs.repository->nv.m_col8 = cols[8] =   8*x/97;    // "Result"
        objs.repository->nv.m_col9 = cols[9] =   5*x/97;    // "ECO"   
        objs.repository->nv.m_col10= cols[10]=  14*x/97;    // "Moves"
    }
    if(true) //temp temp temp white, black, result, moves only
    {
        int x   = (sz.x*98)/100;
        objs.repository->nv.m_col0 = cols[0] =   2*x/97;    // "Game #"
        objs.repository->nv.m_col1 = cols[1] =  14*x/97;    // "White"
        objs.repository->nv.m_col2 = cols[2] =   2*x/97;    // "Elo W"
        objs.repository->nv.m_col3 = cols[3] =  14*x/97;    // "Black"
        objs.repository->nv.m_col4 = cols[4] =   2*x/97;    // "Elo B"
        objs.repository->nv.m_col5 = cols[5] =   2*x/97;    // "Date"
        objs.repository->nv.m_col6 = cols[6] =   2*x/97;    // "Site"
        objs.repository->nv.m_col7 = cols[7] =   2*x/97;    // "Round"
        objs.repository->nv.m_col8 = cols[8] =   8*x/97;    // "Result"
        objs.repository->nv.m_col9 = cols[9] =   2*x/97;    // "ECO"
        objs.repository->nv.m_col10= cols[10]=  45*x/97;    // "Moves"
    }
    list_ctrl->SetColumnWidth( 0, cols[0] );    // "Game #"
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 1, cols[1] );    // "White" 
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 2, cols[2] );    // "Elo W" 
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 3, cols[3] );    // "Black" 
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 4, cols[4] );    // "Elo B" 
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 5, cols[5] );    // "Date"  
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 6, cols[6] );    // "Site"  
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 7, cols[7] );    // "Round"
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 8, cols[8] );    // "Result"
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth( 9, cols[9] );    // "ECO"   
    gc->col_flags.push_back(col_flag);
    list_ctrl->SetColumnWidth(10, cols[10] );   // "Moves"
    gc->col_flags.push_back(col_flag);
    //int top_item;
    //bool resuming = gc->IsResumingPreviousWindow(top_item);
    box_sizer->Add(list_ctrl, 0, wxGROW|wxALL, 5);

    // A dividing line before the details
    wxStaticLine* line = new wxStaticLine ( this, wxID_STATIC,
        wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
    box_sizer->Add(line, 0, wxGROW|wxALL, 5);

    // Create a panel beneath the list control, containing more sizers
    wxBoxSizer* hsiz_panel = new wxBoxSizer(wxHORIZONTAL);
    box_sizer->Add(hsiz_panel, 0, wxALIGN_LEFT|wxALL, 10);
    wxBoxSizer* vsiz_panel_board = new wxBoxSizer(wxVERTICAL);
    wxGridSizer* vsiz_panel_buttons = new wxGridSizer(6,2,0,0);
    wxBoxSizer* vsiz_panel_stats = new wxBoxSizer(wxVERTICAL);
    hsiz_panel->Add(vsiz_panel_board, 0, wxALIGN_TOP|wxALL, 10);
    hsiz_panel->Add(vsiz_panel_buttons, 0, wxALIGN_TOP|wxALL, 10);
    //hsiz_panel->AddSpacer(10);
    hsiz_panel->Add(vsiz_panel_stats, 0, wxALIGN_TOP|wxALL, 10);

    mini_board = new MiniBoard(this);
    list_ctrl->mini_board = mini_board;
    gbl_updated_position = cr;
    mini_board->SetPosition( cr.squares );
    vsiz_panel_board->Add( mini_board, 1, wxALIGN_LEFT|wxALL|wxFIXED_MINSIZE, 5 );
    player_names = new wxStaticText( this, wxID_ANY, "White - Black",
                                           wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL );
    vsiz_panel_board->Add(player_names, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);

    // Load / Ok / Game->Board
    wxButton* ok = new wxButton ( this, wxID_OK, wxT("Load Game"),
        wxDefaultPosition, wxDefaultSize, 0 );
    vsiz_panel_buttons->Add(ok, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    
    // Save all games to a file
    wxButton* save_all_to_a_file = new wxButton ( this, ID_SAVE_ALL_TO_A_FILE, wxT("Save all to a file"),
        wxDefaultPosition, wxDefaultSize, 0 );
    vsiz_panel_buttons->Add(save_all_to_a_file, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);

    // The Cancel button
    wxButton* cancel = new wxButton ( this, wxID_CANCEL,
        wxT("Cancel"), wxDefaultPosition, wxDefaultSize, 0 );
    vsiz_panel_buttons->Add(cancel, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);

    // The Help button
    wxButton* help = new wxButton( this, wxID_HELP, wxT("Help"),
        wxDefaultPosition, wxDefaultSize, 0 );
    vsiz_panel_buttons->Add(help, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);

    wxButton* reload = new wxButton ( this, ID_DB_RELOAD, wxT("Search again"),
                                     wxDefaultPosition, wxDefaultSize, 0 );
    vsiz_panel_buttons->Add(reload, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    

    // Text control for white entry
    text_ctrl = new wxTextCtrl ( this, ID_DB_TEXT, wxT(""), wxDefaultPosition, wxDefaultSize, 0 );
    vsiz_panel_buttons->Add(text_ctrl, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    wxSize sz2=text_ctrl->GetSize();
    text_ctrl->SetSize((sz2.x*118)/32,sz2.y);      // temp temp
    //text_ctrl->SetSize((sz.x*7)/2,sz2.y);      // temp temp
    text_ctrl->SetValue("Name");
    
    filter_ctrl = new wxCheckBox( this, ID_DB_CHECKBOX,
                                 wxT("&Filter"), wxDefaultPosition, wxDefaultSize, 0 );
    vsiz_panel_buttons->Add(filter_ctrl, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    filter_ctrl->SetValue( false );
    
    /*radio_ctrl = new wxRadioButton( this,  ID_DB_RADIO,
     wxT("&Radio"), wxDefaultPosition, wxDefaultSize,  wxRB_GROUP );
     vsiz_panel_buttons->Add(radio_ctrl, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
     radio_ctrl->SetValue( false ); */
    
    wxString combo_array[9];
    combo_array[0] = "Equals";
    combo_array[1] = "Starts with";
    combo_array[2] = "Ends with";
    combo_ctrl = new wxComboBox ( this, ID_DB_COMBO,
                                 "None", wxDefaultPosition,
                                 wxSize(50, wxDefaultCoord), 3, combo_array, wxCB_READONLY );
    vsiz_panel_buttons->Add(combo_ctrl, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    wxString combo;
    combo = "None";
    combo_ctrl->SetValue(combo);
    wxSize sz3=combo_ctrl->GetSize();
    combo_ctrl->SetSize((sz3.x*118)/32,sz3.y);      // temp temp
    
    // Stats list box
//    wxSize sz4 = sz;
//    sz.x /= 4;
//    sz.y /= 3;
    wxSize sz4 = mini_board->GetSize();
    wxSize sz5 = sz4;
    sz5.x = (sz4.x*16)/10;
    sz5.y = (sz4.y*11)/10;
    sz4.x = (sz4.x*13)/10;
    sz4.y = (sz4.y*10)/10;
    notebook = new wxNotebook(this, wxID_ANY, wxDefaultPosition, sz5 );
    //wxPanel *notebook_page1 = new wxPanel(notebook, wxID_ANY );
    vsiz_panel_stats->Add( notebook, 0, wxALIGN_CENTER_VERTICAL|wxGROW|wxALL, 5);
    

}


std::string DbDialog::CalculateMovesColumn( GameDocument &gd )
{
    std::string sp = gd.prefix_txt;
    std::string sm = gd.moves_txt;
    std::string s  = sm;
    int len = sm.length();
    if( len>=1 && sm[len-1] == '*' )
    {
        sm = sm.substr(0,len-1);
        s = sm;
    }
    len = sp.length();
    int start_idx = 0;
    int end_idx = 0;
    for( ; start_idx<len; start_idx++ )
    {
        if( sp[start_idx]!='\n' && sp[start_idx]!='\r' )
            break;
    }
    len -= start_idx;
    end_idx = start_idx;
    for( ; end_idx<len; end_idx++ )
    {
        if( sp[end_idx]=='\n' || sp[end_idx]=='\r' )
            break;
    }
    len = end_idx-start_idx;
    if( len > 0 )
    {
        bool truncate=false;
        if( len > 12 )
        {
            truncate=true;
            len = 12;
        }
        s = "(" + sp.substr(start_idx,start_idx+len-1) + (truncate?"...)":")") + sm;
    }
    return s;
}

// Set the validators for the dialog controls
void DbDialog::SetDialogValidators()
{
/*    FindWindow(ID_HUMAN)->SetValidator(
        wxTextValidator(wxFILTER_ASCII, &dat.m_human));
//    FindWindow(ID_COMPUTER)->SetValidator(
//        wxTextValidator(wxFILTER_ASCII, &dat.m_computer));
    FindWindow(ID_WHITE)->SetValidator(
        wxTextValidator(wxFILTER_ASCII, &dat.m_white));
    FindWindow(ID_BLACK)->SetValidator(
        wxTextValidator(wxFILTER_ASCII, &dat.m_black));
*/
}

// Sets the help text for the dialog controls
void DbDialog::SetDialogHelp()
{
/*
    wxString human_help    = wxT("The person who usually uses this program to play against a chess engine.");
//    wxString computer_help = wxT("An optional friendly name for the chess engine.");
    wxString white_help    = wxT("White's name.");
    wxString black_help    = wxT("Black's name.");

    FindWindow(ID_HUMAN)->SetHelpText(human_help);
    FindWindow(ID_HUMAN)->SetToolTip(human_help);

//    FindWindow(ID_COMPUTER)->SetHelpText(computer_help);
//    FindWindow(ID_COMPUTER)->SetToolTip(computer_help);

    FindWindow(ID_WHITE)->SetHelpText(white_help);
    FindWindow(ID_WHITE)->SetToolTip(white_help);

    FindWindow(ID_BLACK)->SetHelpText(black_help);
    FindWindow(ID_BLACK)->SetToolTip(black_help);
*/
}

void DbDialog::OnListColClick( wxListEvent &event )
{
    int col = event.GetColumn();
}


// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_OK
void DbDialog::OnOkClick( wxCommandEvent& WXUNUSED(event) )
{
    OnOk();
}

void DbDialog::OnActivate(wxActivateEvent& event)
{
    if( !activated_at_least_once )
    {
        activated_at_least_once = true;
        cprintf( "DbDialog::OnActivate\n");
        wxPoint pos = notebook->GetPosition();
        //list_ctrl_stats->Hide();
        //list_ctrl_transpo->Hide();
        
        utility = new wxButton ( this, ID_DB_UTILITY, wxT("Calculate Stats"),
                                pos, wxDefaultSize, 0 );
        wxSize sz_panel  = notebook->GetSize();
        wxSize sz_button = utility->GetSize();
        wxPoint pos_button = utility->GetPosition();
        pos_button.x += (sz_panel.x/2 - sz_button.x/2);
        pos_button.y += (sz_panel.y/2 - sz_button.y/2);
        utility->SetPosition( pos_button );
        
        //vsiz_panel_buttons->Add(utility, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
        list_ctrl->ReadItem(0);
    }
}


void DbDialog::LoadGame( int idx )
{
    if( list_ctrl )
    {
        static DB_GAME_INFO info;
        objs.db->GetRow( &info, idx );
        GameDocument gd;
        std::vector<thc::Move> moves;
        gd.white = info.white;
        gd.black = info.black;
        gd.result = info.result;
        int len = info.str_blob.length();
        const char *blob = info.str_blob.c_str();
        CompressMoves press;
        for( int nbr=0; nbr<len;  )
        {
            thc::ChessRules cr = press.cr;
            thc::Move mv;
            int nbr_used = press.decompress_move( blob, mv );
            if( nbr_used == 0 )
                break;
            moves.push_back(mv);
            blob += nbr_used;
            nbr += nbr_used;
        }
        gd.LoadFromMoveList(moves);
        db_game = gd;
        db_game_set = true;
    }
}


void DbDialog::OnListSelected( wxListEvent &event )
{
    if( list_ctrl )
    {
        int idx = event.m_itemIndex;
        cprintf( "DbDialog::OnListSelected(%d)\n", idx );
        list_ctrl->SetItemState( idx, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED );
        LoadGame( idx );
        TransferDataToWindow();
        AcceptAndClose();
    }
}

void DbDialog::OnListFocused( wxListEvent &event )
{
    if( list_ctrl )
    {
        int prev = list_ctrl->focus_idx;
        int idx = event.m_itemIndex;
        cprintf( "DbDialog::OnListFocused() Prev idx=%d, New idx=%d\n", prev, idx );
        list_ctrl->ReceiveFocus( idx );
        list_ctrl->RefreshItem(prev);
    }
}



// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_OK
void DbDialog::OnOk()
{
    if( list_ctrl )
    {
        LoadGame( list_ctrl->focus_idx );
        TransferDataToWindow();
        AcceptAndClose();
    }
}

bool DbDialog::LoadGame( GameLogic *gl, GameDocument& gd, int &file_game_idx )
{
    if( db_game_set )
    {
        gd = db_game;
    }
    return db_game_set;
}


void DbDialog::OnSaveAllToAFile( wxCommandEvent& WXUNUSED(event) )
{
    wxFileDialog fd( objs.frame, "Save all listed games to a new .pgn file", "", "", "*.pgn", wxFD_SAVE|wxFD_OVERWRITE_PROMPT );
    wxString dir = objs.repository->nv.m_doc_dir;
    fd.SetDirectory(dir);
    int answer = fd.ShowModal();
    if( answer == wxID_OK )
    {
        wxString dir;
        wxFileName::SplitPath( fd.GetPath(), &dir, NULL, NULL );
        objs.repository->nv.m_doc_dir = dir;
        wxString wx_filename = fd.GetPath();
        std::string filename( wx_filename.c_str() );
        gc->FileSaveAllAsAFile( filename );
    }
}


void DbDialog::OnCancel( wxCommandEvent& WXUNUSED(event) )
{
    if( list_ctrl )
    {
        gc->PrepareResumePreviousWindow( list_ctrl->GetTopItem() );
        int sz=gc->gds.size();
        for( int i=0; i<sz; i++ )
        {
            gc->gds[i]->selected =  ( wxLIST_STATE_SELECTED & list_ctrl->GetItemState(i,wxLIST_STATE_SELECTED)) ? true : false;
            gc->gds[i]->focus    =  ( wxLIST_STATE_FOCUSED & list_ctrl->GetItemState(i,wxLIST_STATE_FOCUSED)  ) ? true : false;
        }
    }
    EndDialog( wxID_CANCEL );
}

// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_HELP
void DbDialog::OnHelpClick( wxCommandEvent& WXUNUSED(event) )
{
    // Normally we would wish to display proper online help.
    // For this example, we're just using a message box.
    /*
    wxGetApp().GetHelpController().DisplaySection(wxT("Personal record dialog"));
     */

    wxString helpText =
    wxT("Use this panel to load games from the database\n\n");
    wxMessageBox(helpText,
    wxT("Database Dialog Help"),
    wxOK|wxICON_INFORMATION, this);
}

void DbDialog::OnRadio( wxCommandEvent& event )
{
}

void DbDialog::OnSpin( wxCommandEvent& event )
{
}

void DbDialog::OnComboBox( wxCommandEvent& event )
{
}

void DbDialog::OnCheckBox( wxCommandEvent& event )
{
    cprintf( "OnCheckBox()\n");
    list_ctrl->SetItemCount(20);//gbl_nbr);
    list_ctrl->RefreshItems(0,19);
}


void DbDialog::OnReload( wxCommandEvent& WXUNUSED(event) )
{
    wxString name = text_ctrl->GetValue();
    std::string sname(name.c_str());
    thc::ChessPosition start_cp;
    
    // Temp - do a "find on page type feature"
    if( sname.length()>0 && cr==start_cp )
    {
        int row = objs.db->FindRow( sname );
        list_ctrl->EnsureVisible( row );   // get vaguely close
        list_ctrl->SetItemState( row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
        list_ctrl->SetItemState( row, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED );
    }
    else
    {
        gbl_nbr = objs.db->SetPosition( cr, sname );
        char buf[200];
        sprintf(buf,"List of %d matching games from the database",gbl_nbr);
        title_ctrl->SetLabel( buf );
        cprintf( "Reloading, %d games\n", gbl_nbr);
        list_ctrl->SetItemCount(gbl_nbr);
        list_ctrl->RefreshItems(0,gbl_nbr-1);
    }
}

// Sorting map<std::string,MOV_STATS> by MOVE_STATS instead of std::string requires this flipping procedure
template<typename A, typename B>
std::pair<B,A> flip_pair(const std::pair<A,B> &p)
{
    return std::pair<B,A>(p.second, p.first);
}

template<typename A, typename B>
std::multimap<B,A> flip_and_sort_map(const std::map<A,B> &src)
{
    std::multimap<B,A> dst;
#ifdef MAC_FIX_LATER    // doesn't compile in Visual C++
    std::transform(src.begin(), src.end(), std::inserter(dst, dst.begin()),
                   flip_pair<A,B>);
#endif
    return dst;
}

class AutoTimer
{
public:
    void Begin();
    double End();
    AutoTimer( const char *desc )
    {
        this->desc = desc;
        Begin();
    }
    ~AutoTimer()
    {
        double elapsed = End();;
        cprintf( "%s: time elapsed (ms) = %f\n", desc, elapsed );
    }
private:
#ifdef THC_WINDOWS
    LARGE_INTEGER frequency;        // ticks per second
    LARGE_INTEGER t1, t2;           // ticks
#endif
#ifdef THC_MAC
    timeval t1, t2;
#endif
    const char *desc;
};


void AutoTimer::Begin()
{
#ifdef THC_WINDOWS
    // get ticks per second
    QueryPerformanceFrequency(&frequency);
    
    // start timer
    QueryPerformanceCounter(&t1);
#endif
#ifdef THC_MAC
    // start timer
    gettimeofday(&t1, NULL);
#endif
}

double AutoTimer::End()
{
    double elapsed_time;
#ifdef THC_WINDOWS
    // stop timer
    QueryPerformanceCounter(&t2);

    // compute the elapsed time in millisec
    elapsed_time = (t2.QuadPart - t1.QuadPart) * 1000.0 / frequency.QuadPart;
#endif
#ifdef THC_MAC
    // stop timer
    gettimeofday(&t2, NULL);
    
    // compute the elapsed time in millisec
    elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
    elapsed_time += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms
#endif
    return elapsed_time;
}

void DbDialog::OnUtility( wxCommandEvent& WXUNUSED(event) )
{
    {
        AutoTimer at("1) Load games into memory");
        
        // Load all the matching games from the database
        objs.db->LoadAllGames( cache, gbl_nbr );
    }
    {
        AutoTimer at("2) Calculate stats");
        moves_from_base_position.clear();
        moves_in_this_position.clear();
        StatsCalculate();
    }
}

void DbDialog::StatsCalculate()
{
    transpositions.clear();
    stats.clear();
    games.clear();
    cprintf( "Remove focus %d\n", list_ctrl->focus_idx );
    list_ctrl->SetItemState( list_ctrl->focus_idx, 0, wxLIST_STATE_FOCUSED );
    list_ctrl->SetItemState( list_ctrl->focus_idx, 0, wxLIST_STATE_SELECTED );

    
    thc::ChessRules cr_to_match = this->cr;
    bool add_go_back = false;
    std::string go_back_string;
    for( int i=0; i<moves_from_base_position.size(); i++ )
    {
        thc::Move mv = moves_from_base_position[i];
        if( i+1 == moves_from_base_position.size() )
        {
            std::string s = mv.NaturalOut(&cr_to_match);
            if( !cr_to_match.white )
                s = "..." + s;
            go_back_string = "Go back (undo " + s + ")";
            add_go_back = true;
        }
        cr_to_match.PlayMove(mv);
    }
    extern void db_set_gbl_position( thc::ChessPosition &pos );   // FIXME this is an abomination
    db_set_gbl_position( cr_to_match );   // FIXME this is an abomination

    // hash to match
    uint64_t gbl_hash = cr_to_match.Hash64Calculate();
    
    int maxlen = 1000000;   // absurdly large until a match found

    // For each cached game
    for( unsigned int i=0; i<cache.size(); i++ )
    {
        DB_GAME_INFO info = cache[i];
    
        // Search for a match to this game
        bool new_transposition_found=false;
        bool found=false;
        int found_idx=0;
        for( unsigned int j=0; !found && j<transpositions.size(); j++ )
        {
            std::string &this_one = transpositions[j].blob;
            const char *p = this_one.c_str();
            size_t len = this_one.length();
            if( info.str_blob.length()>=len && 0 == memcmp(p,info.str_blob.c_str(),len) )
            {
                found = true;
                found_idx = j;
            }
        }
        
        // If none so far add the one from this game
        if( !found )
        {
            PATH_TO_POSITION ptp;
            size_t len = info.str_blob.length();
            const char *blob = (const char*)info.str_blob.c_str();
            uint64_t hash = ptp.press.cr.Hash64Calculate();
            int nbr=0;
            found = (hash==gbl_hash && ptp.press.cr==cr_to_match );
            while( !found && nbr<len && nbr<maxlen )
            {
                thc::ChessRules cr_hash = ptp.press.cr;
                thc::Move mv;
                int nbr_used = ptp.press.decompress_move( blob, mv );
                if( nbr_used == 0 )
                    break;
                blob += nbr_used;
                nbr += nbr_used;
                hash = cr_hash.Hash64Update( hash, mv );
                if( hash == gbl_hash && ptp.press.cr==cr_to_match )
                    found = true;
            }
            if( found )
            {
                maxlen = nbr+8;
                new_transposition_found = true;
                ptp.blob =info.str_blob.substr(0,nbr);
                transpositions.push_back(ptp);
                found_idx = transpositions.size()-1;
            }
        }

        if( found )
        {
            games.push_back(info);
            PATH_TO_POSITION *p = &transpositions[found_idx];
            p->frequency++;
            size_t len = p->blob.length();
            if( len < info.str_blob.length() ) // must be more moves
            {
                const char *compress_move_ptr = info.str_blob.c_str()+len;
                thc::Move mv;
                p->press.decompress_move_stay( compress_move_ptr, mv );
                uint32_t imv = 0;
                assert( sizeof(imv) == sizeof(mv) );
                memcpy( &imv, &mv, sizeof(mv) ); // FIXME
                std::map< uint32_t, MOVE_STATS >::iterator it;
                it = stats.find(imv);
                if( it == stats.end() )
                {
                    MOVE_STATS empty;
                    empty.nbr_games = 0;
                    empty.nbr_white_wins = 0;
                    empty.nbr_black_wins = 0;
                    empty.nbr_draws = 0;
                    stats[imv] = empty;
                    it = stats.find(imv);
                }
                it->second.nbr_games++;
                if( info.result == "1-0" )
                    it->second.nbr_white_wins++;
                else if( info.result== "0-1" )
                    it->second.nbr_black_wins++;
                else if( info.result== "1/2-1/2" )
                    it->second.nbr_draws++;
            }
        }
        
        if( new_transposition_found )
        {
            std::sort( transpositions.rbegin(), transpositions.rend() );
        }
    }

    wxArrayString strings;
    if( !list_ctrl_stats )
    {
        utility->Hide();
        wxSize sz4 = mini_board->GetSize();
        sz4.x = (sz4.x*13)/10;
        sz4.y = (sz4.y*10)/10;

        list_ctrl_stats   = new wxListBox( notebook, ID_DB_LISTBOX_STATS, wxDefaultPosition, sz4, 0, NULL, wxLB_HSCROLL );
        list_ctrl_transpo = new wxListBox( notebook, ID_DB_LISTBOX_TRANSPO, wxDefaultPosition, sz4, 0, NULL, wxLB_HSCROLL );
        notebook->AddPage(list_ctrl_stats,"Next Move",true);
        notebook->AddPage(list_ctrl_transpo,"Transpositions",false);
    }

    list_ctrl_stats->Clear();
    moves_in_this_position.clear();

    // Sort the stats according to number of games
    std::multimap< MOVE_STATS,  uint32_t > dst = flip_and_sort_map(stats);
    std::multimap< MOVE_STATS,  uint32_t >::reverse_iterator it;
    for( it=dst.rbegin(); it!=dst.rend(); it++ )
    {
        double percentage_score;
        int nbr_games      = it->first.nbr_games;
        int nbr_white_wins = it->first.nbr_white_wins;
        int nbr_black_wins = it->first.nbr_black_wins;
        int nbr_draws      = it->first.nbr_draws;
        int draws_plus_no_result = nbr_games - nbr_white_wins - nbr_black_wins;
        percentage_score = ((1.0*nbr_white_wins + 0.5*draws_plus_no_result) * 100.0) / nbr_games;
        uint32_t imv=it->second;
        thc::Move mv;
        memcpy( &mv, &imv, sizeof(mv) ); // FIXME
        if( add_go_back )
        {
            add_go_back = false;
            wxString wstr( go_back_string.c_str() );
            strings.Add(wstr);
            moves_in_this_position.push_back(mv);
        }
        moves_in_this_position.push_back(mv);
        std::string s = mv.NaturalOut(&cr_to_match);
        if( !cr_to_match.white )
            s = "..." + s;
        char buf[200];
        sprintf( buf, "%s: %d games, white scores %.1f%% +%d -%d =%d",
                s.c_str(),
                nbr_games, percentage_score,
                nbr_white_wins, nbr_black_wins, nbr_draws );
        cprintf( "%s\n", buf );
        wxString wstr(buf);
        strings.Add(wstr);
    }
    list_ctrl_stats->InsertItems( strings, 0 );

    // Print the transpositions in order
    list_ctrl_transpo->Clear();
    strings.Clear();
    cprintf( "%d transpositions\n", transpositions.size() );
    for( unsigned int j=0; j<transpositions.size(); j++ )
    {
        PATH_TO_POSITION *p = &transpositions[j];
        const char *blob = p->blob.c_str();
        size_t len = p->blob.length();
        std::string txt;
        CompressMoves press;
        int nbr = 0;
        int count = 0;
        while( nbr < len )
        {
            thc::ChessRules cr_before = press.cr;
            thc::Move mv;
            int nbr_used = press.decompress_move( blob, mv );
            if( nbr_used == 0 )
                break;
            if( count%2 == 0 )
            {
                char buf[100];
                sprintf( buf, "%d.", count/2+1 );
                txt += buf;
            }
            txt += mv.NaturalOut(&cr_before);
            txt += " ";
            blob += nbr_used;
            nbr += nbr_used;
            count++;
        }
        char buf[200];
        sprintf( buf, "T%d: %s: %d occurences", j+1, txt.c_str(), p->frequency );
        cprintf( "%s\n", buf );
        wxString wstr(buf);
        strings.Add(wstr);
    }
    list_ctrl_transpo->InsertItems( strings, 0 );

    gbl_nbr = games.size();
    list_ctrl->SetItemCount(gbl_nbr);
    list_ctrl->RefreshItems( 0, gbl_nbr-1 );
    list_ctrl->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    list_ctrl->ReceiveFocus(0);
    char buf[200];
    sprintf(buf,"List of %d matching games from the database",gbl_nbr);
    title_ctrl->SetLabel( buf );

    int top = list_ctrl->GetTopItem();
    int count = 1 + list_ctrl->GetCountPerPage();
    if( count > gbl_nbr )
        count = gbl_nbr;
    for( int i=0; i<count; i++ )
        list_ctrl->RefreshItem(top++);
    list_ctrl->SetFocus();
}

// Move Stats or Transpostitions selected
void DbDialog::OnTabSelected( wxBookCtrlEvent& event )
{
    transpo_activated = (1==event.GetSelection());
    int top = list_ctrl->GetTopItem();
    int count = 1 + list_ctrl->GetCountPerPage();
    if( count > gbl_nbr )
        count = gbl_nbr;
    for( int i=0; i<count; i++ )
        list_ctrl->RefreshItem(top++);
}


// One of the moves in move stats is clicked
void DbDialog::OnNextMove( wxCommandEvent &event )
{
    int idx = event.GetSelection();
    cprintf( "DbDialog::OnNextMove(%d)\n", idx );
    if( idx==0 && moves_from_base_position.size()>0 )
    {
        moves_from_base_position.pop_back();
    }
    else
    {
        thc::Move this_one = moves_in_this_position[idx];
        moves_from_base_position.push_back(this_one);
    }
    StatsCalculate();
}


/*
    games.clear();
    
    
    // We calculate a vector of all blobs in the games that leading to the search position
    std::vector< PATH_TO_POSITION > transpositions;
    
    // Map each move in the position to move stats
    std::map< uint32_t, MOVE_STATS > stats;
    
    // hash to match
    uint64_t gbl_hash = cr.Hash64Calculate();
    
    // For each game
    for( unsigned int game_idx=0; game_idx<games.size(); game_idx++ )
    {
        DB_GAME_INFO info = games[game_idx];
        
        // Search for a match to this game
        bool new_transposition_found=false;
        bool found=false;
        int found_idx=0;
        for( unsigned int j=0; !found && j<transpositions.size(); j++ )
        {
            std::string &this_one = transpositions[j].blob;
            const char *p = this_one.c_str();
            size_t len = this_one.length();
            if( info.str_blob.length()>=len && 0 == memcmp(p,info.str_blob.c_str(),len) )
            {
                found = true;
                found_idx = j;
            }
        }
        
        // If none so far add the one from this game
        if( !found )
        {
            PATH_TO_POSITION ptp;
            size_t len = info.str_blob.length();
            const char *blob = (const char*)info.str_blob.c_str();
            uint64_t hash = ptp.press.cr.Hash64Calculate();
            int nbr=0;
            found = (hash==gbl_hash && ptp.press.cr==this->cr );
            while( !found && nbr<len )
            {
                thc::ChessRules cr = ptp.press.cr;
                thc::Move mv;
                int nbr_used = ptp.press.decompress_move( blob, mv );
                if( nbr_used == 0 )
                    break;
                blob += nbr_used;
                nbr += nbr_used;
                hash = cr.Hash64Update( hash, mv );
                if( hash == gbl_hash && ptp.press.cr==this->cr )
                    found = true;
            }
            if( found )
            {
                new_transposition_found = true;
                ptp.blob =info.str_blob.substr(0,nbr);
                transpositions.push_back(ptp);
                found_idx = transpositions.size()-1;
            }
        }
        
        if( found )
        {
            PATH_TO_POSITION *p = &transpositions[found_idx];
            p->frequency++;
            size_t len = p->blob.length();
            if( len < info.str_blob.length() ) // must be more moves
            {
                const char *compress_move_ptr = info.str_blob.c_str()+len;
                thc::Move mv;
                p->press.decompress_move_stay( compress_move_ptr, mv );
                if( mv == this_one )
                {
                    //cache_subset.push_back( game_idx );
                }
            }
        }
        if( new_transposition_found )
        {
            std::sort( transpositions.rbegin(), transpositions.rend() );
        }
    }
    list_ctrl->SetItemCount( games.size() );
    list_ctrl->ReceiveFocus(0);
    list_ctrl->RefreshItems( 0, games.size()-1 );
    list_ctrl->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
} */


bool DbDialog::ShowModalOk()
{
    bool ok = (wxID_OK == ShowModal());
    objs.repository->nv.m_col0  = list_ctrl->GetColumnWidth( 0 );    // "Game #"
    objs.repository->nv.m_col1  = list_ctrl->GetColumnWidth( 1 );    // "White"
    objs.repository->nv.m_col2  = list_ctrl->GetColumnWidth( 2 );    // "Elo W"
    objs.repository->nv.m_col3  = list_ctrl->GetColumnWidth( 3 );    // "Black"
    objs.repository->nv.m_col4  = list_ctrl->GetColumnWidth( 4 );    // "Elo B" 
    objs.repository->nv.m_col5  = list_ctrl->GetColumnWidth( 5 );    // "Date"  
    objs.repository->nv.m_col6  = list_ctrl->GetColumnWidth( 6 );    // "Site"  
    objs.repository->nv.m_col7  = list_ctrl->GetColumnWidth( 7 );    // "Round" 
    objs.repository->nv.m_col8  = list_ctrl->GetColumnWidth( 8 );    // "Result"
    objs.repository->nv.m_col9  = list_ctrl->GetColumnWidth( 9 );    // "ECO"   
    objs.repository->nv.m_col10 = list_ctrl->GetColumnWidth(10 );    // "Moves"
    return ok;
}
