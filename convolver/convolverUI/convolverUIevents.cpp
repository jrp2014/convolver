/////////////////////////////////////////////////////////////////////////////
//
// convolverUIEvents.cpp : Implementation of CConvolverUI events
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "convolverUI.h"

void CConvolverUI::OpenStateChange( long NewState )
{
    switch (NewState)
    {
    case wmposUndefined:
        break;
	case wmposPlaylistChanging:
        break;
	case wmposPlaylistLocating:
        break;
	case wmposPlaylistConnecting:
        break;
	case wmposPlaylistLoading:
        break;
	case wmposPlaylistOpening:
        break;
	case wmposPlaylistOpenNoMedia:
        break;
	case wmposPlaylistChanged:
        break;
	case wmposMediaChanging:
        break;
	case wmposMediaLocating:
        break;
	case wmposMediaConnecting:
        break;
	case wmposMediaLoading:
        break;
	case wmposMediaOpening:
        break;
	case wmposMediaOpen:
        break;
	case wmposBeginCodecAcquisition:
        break;
	case wmposEndCodecAcquisition:
        break;
	case wmposBeginLicenseAcquisition:
        break;
	case wmposEndLicenseAcquisition:
        break;
	case wmposBeginIndividualization:
        break;
	case wmposEndIndividualization:
        break;
	case wmposMediaWaiting:
        break;
	case wmposOpeningUnknownURL:
        break;
    default:
        break;
    }
}

void CConvolverUI::PlayStateChange( long NewState )
{
    switch (NewState)
    {
    case wmppsUndefined:
        break;
	case wmppsStopped:
        break;
	case wmppsPaused:
        break;
	case wmppsPlaying:
        break;
	case wmppsScanForward:
        break;
	case wmppsScanReverse:
        break;
	case wmppsBuffering:
        break;
	case wmppsWaiting:
        break;
	case wmppsMediaEnded:
        break;
	case wmppsTransitioning:
        break;
	case wmppsReady:
        break;
	case wmppsReconnecting:
        break;
	case wmppsLast:
        break;
    default:
        break;
    }
}

void CConvolverUI::AudioLanguageChange( long LangID )
{
}

void CConvolverUI::StatusChange()
{
}

void CConvolverUI::ScriptCommand( BSTR scType, BSTR Param )
{
}

void CConvolverUI::NewStream()
{
}

void CConvolverUI::Disconnect( long Result )
{
}

void CConvolverUI::Buffering( VARIANT_BOOL Start )
{
}

void CConvolverUI::Error()
{
    CComPtr<IWMPError>      spError;
    CComPtr<IWMPErrorItem>  spErrorItem;
    HRESULT                 dwError = S_OK;
    HRESULT                 hr = S_OK;

    if (m_spCore)
    {
        hr = m_spCore->get_error(&spError);

        if (SUCCEEDED(hr))
        {
            hr = spError->get_item(0, &spErrorItem);
        }

        if (SUCCEEDED(hr))
        {
            hr = spErrorItem->get_errorCode( (long *) &dwError );
        }
    }
}

void CConvolverUI::Warning( long WarningType, long Param, BSTR Description )
{
}

void CConvolverUI::EndOfStream( long Result )
{
}

void CConvolverUI::PositionChange( double oldPosition, double newPosition)
{
}

void CConvolverUI::MarkerHit( long MarkerNum )
{
}

void CConvolverUI::DurationUnitChange( long NewDurationUnit )
{
}

void CConvolverUI::CdromMediaChange( long CdromNum )
{
}

void CConvolverUI::PlaylistChange( IDispatch * Playlist, WMPPlaylistChangeEventType change )
{
    switch (change)
    {
    case wmplcUnknown:
        break;
	case wmplcClear:
        break;
	case wmplcInfoChange:
        break;
	case wmplcMove:
        break;
	case wmplcDelete:
        break;
	case wmplcInsert:
        break;
	case wmplcAppend:
        break;
	case wmplcPrivate:
        break;
	case wmplcNameChange:
        break;
	case wmplcMorph:
        break;
	case wmplcSort:
        break;
	case wmplcLast:
        break;
    default:
        break;
    }
}

void CConvolverUI::CurrentPlaylistChange( WMPPlaylistChangeEventType change )
{
    switch (change)
    {
    case wmplcUnknown:
        break;
	case wmplcClear:
        break;
	case wmplcInfoChange:
        break;
	case wmplcMove:
        break;
	case wmplcDelete:
        break;
	case wmplcInsert:
        break;
	case wmplcAppend:
        break;
	case wmplcPrivate:
        break;
	case wmplcNameChange:
        break;
	case wmplcMorph:
        break;
	case wmplcSort:
        break;
	case wmplcLast:
        break;
    default:
        break;
    }
}

void CConvolverUI::CurrentPlaylistItemAvailable( BSTR bstrItemName )
{
}

void CConvolverUI::MediaChange( IDispatch * Item )
{
}

void CConvolverUI::CurrentMediaItemAvailable( BSTR bstrItemName )
{
}

void CConvolverUI::CurrentItemChange( IDispatch *pdispMedia)
{
}

void CConvolverUI::MediaCollectionChange()
{
}

void CConvolverUI::MediaCollectionAttributeStringAdded( BSTR bstrAttribName,  BSTR bstrAttribVal )
{
}

void CConvolverUI::MediaCollectionAttributeStringRemoved( BSTR bstrAttribName,  BSTR bstrAttribVal )
{
}

void CConvolverUI::MediaCollectionAttributeStringChanged( BSTR bstrAttribName, BSTR bstrOldAttribVal, BSTR bstrNewAttribVal)
{
}

void CConvolverUI::PlaylistCollectionChange()
{
}

void CConvolverUI::PlaylistCollectionPlaylistAdded( BSTR bstrPlaylistName)
{
}

void CConvolverUI::PlaylistCollectionPlaylistRemoved( BSTR bstrPlaylistName)
{
}

void CConvolverUI::PlaylistCollectionPlaylistSetAsDeleted( BSTR bstrPlaylistName, VARIANT_BOOL varfIsDeleted)
{
}

void CConvolverUI::ModeChange( BSTR ModeName, VARIANT_BOOL NewValue)
{
}

void CConvolverUI::MediaError( IDispatch * pMediaObject)
{
}

void CConvolverUI::OpenPlaylistSwitch( IDispatch *pItem )
{
}

void CConvolverUI::DomainChange( BSTR strDomain)
{
}

void CConvolverUI::SwitchedToPlayerApplication()
{
}

void CConvolverUI::SwitchedToControl()
{
}

void CConvolverUI::PlayerDockedStateChange()
{
}

void CConvolverUI::PlayerReconnect()
{
}

void CConvolverUI::Click( short nButton, short nShiftState, long fX, long fY )
{
}

void CConvolverUI::DoubleClick( short nButton, short nShiftState, long fX, long fY )
{
}

void CConvolverUI::KeyDown( short nKeyCode, short nShiftState )
{
}

void CConvolverUI::KeyPress( short nKeyAscii )
{
}

void CConvolverUI::KeyUp( short nKeyCode, short nShiftState )
{
}

void CConvolverUI::MouseDown( short nButton, short nShiftState, long fX, long fY )
{
}

void CConvolverUI::MouseMove( short nButton, short nShiftState, long fX, long fY )
{
}

void CConvolverUI::MouseUp( short nButton, short nShiftState, long fX, long fY )
{
}
