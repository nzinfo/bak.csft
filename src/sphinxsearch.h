//
// $Id$
//

//
// Copyright (c) 2001-2009, Andrew Aksyonoff. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#ifndef _sphinxsearch_
#define _sphinxsearch_

#include "sphinx.h"

//////////////////////////////////////////////////////////////////////////
// PACKED HIT MACROS
//////////////////////////////////////////////////////////////////////////

/// pack hit
#define HIT_PACK(_field,_pos)	( ((_field)<<24) | ((_pos)&0x7fffffUL) )

/// extract in-field position from packed hit
#define HIT2POS(_hit)			((_hit)&0x7fffffUL)

/// extract field number from packed hit
#define HIT2FIELD(_hit)			((_hit)>>24)

/// prepare hit for LCS counting
#define HIT2LCS(_hit)			(_hit&0xff7fffffUL)

/// field end flag
#define HIT_FIELD_END			0x800000UL

//////////////////////////////////////////////////////////////////////////

/// term modifiers
enum TermPosFilter_e
{
	TERM_POS_FIELD_LIMIT = 1,
	TERM_POS_FIELD_START,
	TERM_POS_FIELD_END,
	TERM_POS_FIELD_STARTEND
};


/// term, searcher view
class ISphQword
{
public:
	// setup by query parser
	CSphString		m_sWord;		///< my copy of word
	CSphString		m_sDictWord;	///< word after being processed by dict (eg. stemmed)
	SphWordID_t		m_iWordID;		///< word ID, from dictionary
	int				m_iQueryPos;	///< word position, from query
	int				m_iTermPos;
	int				m_iAtomPos;

	// setup by QwordSetup()
	int				m_iDocs;		///< document count, from wordlist
	int				m_iHits;		///< hit count, from wordlist
	bool			m_bHasHitlist;	///< hitlist  presence flag

	// iterator state
	DWORD			m_uFields;		///< current match fields
	DWORD			m_uMatchHits;	///< current match hits count
	SphOffset_t		m_iHitlistPos;	///< current position in hitlist, from doclist

public:
	ISphQword ()
		: m_iWordID ( 0 )
		, m_iQueryPos ( -1 )
		, m_iTermPos ( 0 )
		, m_iAtomPos ( 0 )
		, m_iDocs ( 0 )
		, m_iHits ( 0 )
		, m_bHasHitlist ( true )
		, m_uFields ( 0 )
		, m_uMatchHits ( 0 )
		, m_iHitlistPos ( 0 )
	{}

	virtual const CSphMatch &	GetNextDoc ( DWORD * pInlineDocinfo ) = 0;
	virtual void				SeekHitlist ( SphOffset_t uOff ) = 0;
	virtual DWORD				GetNextHit () = 0;
};


/// term setup, searcher view
class ISphQwordSetup : ISphNoncopyable
{
public:
	CSphDict *				m_pDict;
	const CSphIndex *		m_pIndex;
	ESphDocinfo				m_eDocinfo;
	CSphDocInfo				m_tMin;
	int						m_iToCalc;
	int64_t					m_iMaxTimer;
	CSphString *			m_pWarning;

	ISphQwordSetup ()
		: m_pDict ( NULL )
		, m_pIndex ( NULL )
		, m_eDocinfo ( SPH_DOCINFO_NONE )
		, m_iToCalc ( 0 )
		, m_iMaxTimer ( 0 )
		, m_pWarning ( NULL )
	{}

	virtual ~ISphQwordSetup () {}
};

//////////////////////////////////////////////////////////////////////////

/// generic ranker interface
class ISphRanker
{
public:
	virtual CSphMatch *			GetMatchesBuffer() = 0;
	virtual int					GetMatches ( int iFields, const int * pWeights ) = 0;
};

/// factory
ISphRanker * sphCreateRanker ( const CSphQuery * pQuery, const char * sQuery, CSphQueryResult * pResult, const ISphQwordSetup & tTermSetup, CSphString & sError );

#endif // _sphinxsearch_
//
// $Id$
//
