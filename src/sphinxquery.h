//
// $Id$
//

//
// Copyright (c) 2001-2012, Andrew Aksyonoff
// Copyright (c) 2008-2012, Sphinx Technologies Inc
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#ifndef _sphinxquery_
#define _sphinxquery_

#include "sphinx.h"

#include "rapidjson/writer.h"	// for stringify JSON
#include "rapidjson/filestream.h"	// wrapper of C stream for prettywriter as output
#include "rapidjson/document.h"
#include <cstdio>

//////////////////////////////////////////////////////////////////////////////

enum XQStarPosition
{
	STAR_NONE	= 0,
	STAR_FRONT	= 1,
	STAR_BACK	= 2,
	STAR_BOTH	= 3
};

/// extended query word with attached position within atom
struct XQKeyword_t
{
	CSphString			m_sWord;
	int					m_iAtomPos;
	bool				m_bFieldStart;	///< must occur at very start
	bool				m_bFieldEnd;	///< must occur at very end
	DWORD				m_uStarPosition;
	bool				m_bExpanded;	///< added by prefix expansion
	bool				m_bExcluded;	///< excluded by query (rval to operator NOT)

	XQKeyword_t ()
		: m_iAtomPos ( -1 )
		, m_bFieldStart ( false )
		, m_bFieldEnd ( false )
		, m_uStarPosition ( STAR_NONE )
		, m_bExpanded ( false )
		, m_bExcluded ( false )
	{}

	XQKeyword_t ( const char * sWord, int iPos )
		: m_sWord ( sWord )
		, m_iAtomPos ( iPos )
		, m_bFieldStart ( false )
		, m_bFieldEnd ( false )
		, m_uStarPosition ( STAR_NONE )
		, m_bExpanded ( false )
		, m_bExcluded ( false )
	{}

    // the word..
    template <typename Writer>
    void Serialize(Writer& writer) const {
        /* \# \$, if # | $ at begin | end of the term, stands for m_bFieldStart | m_bFieldEnd  */
        writer.StartObject();

        writer.String("term");
        writer.String(m_sWord.cstr());

        writer.String("pos");
        writer.Uint(m_iAtomPos);

        writer.EndObject();
    }
};


/// extended query operator
enum XQOperator_e
{
	SPH_QUERY_AND,
	SPH_QUERY_OR,
	SPH_QUERY_NOT,
	SPH_QUERY_ANDNOT,
	SPH_QUERY_BEFORE,
	SPH_QUERY_PHRASE,
	SPH_QUERY_PROXIMITY,
	SPH_QUERY_QUORUM,
	SPH_QUERY_NEAR,
	SPH_QUERY_SENTENCE,
	SPH_QUERY_PARAGRAPH
};

// the limit of field or zone
struct XQLimitSpec_t
{
	bool					m_bFieldSpec;	///< whether field spec was already explicitly set
	CSphSmallBitvec			m_dFieldMask;	///< fields mask (spec part)
	int						m_iFieldMaxPos;	///< max position within field (spec part)
	CSphVector<int>			m_dZones;		///< zone indexes in per-query zones list

public:
	XQLimitSpec_t ()
	{
		Reset();
	}

	inline void Reset ()
	{
		m_bFieldSpec = false;
		m_iFieldMaxPos = 0;
		m_dFieldMask.Set();
		m_dZones.Reset();
	}

	XQLimitSpec_t ( const XQLimitSpec_t& dLimit )
	{
		if ( this==&dLimit )
			return;
		Reset();
		*this = dLimit;
	}

	XQLimitSpec_t & operator = ( const XQLimitSpec_t& dLimit )
	{
		if ( this==&dLimit )
			return *this;

		if ( dLimit.m_bFieldSpec )
			SetFieldSpec ( dLimit.m_dFieldMask, dLimit.m_iFieldMaxPos );

		if ( dLimit.m_dZones.GetLength() )
			SetZoneSpec ( dLimit.m_dZones );

		return *this;
	}

    template <typename Writer>
    void Serialize(Writer& writer) const {
        // m_iFieldMaxPos
        const CSphSchema * pSchema = (const CSphSchema * )writer.getData();
        if(pSchema && m_bFieldSpec) {
            writer.StartArray();
            for(int i = 0; i < SPH_MAX_FIELDS; i++){
              if(m_dFieldMask.Test(i)){
                  writer.String(pSchema->m_dFields[i].m_sName.cstr());
                 printf("column %s %d\n", pSchema->m_dFields[i].m_sName.cstr(), pSchema->m_dFields[i].m_iIndex);
              }
            }
            writer.EndArray();
            printf("-------------\n");
        }
    }

public:
	void SetZoneSpec ( const CSphVector<int> & dZones );
	void SetFieldSpec ( const CSphSmallBitvec& uMask, int iMaxPos );
};

/// extended query node
/// plain nodes are just an atom
/// non-plain nodes are a logical function over children nodes
struct XQNode_t : public ISphNoncopyable
{
	XQNode_t *				m_pParent;		///< my parent node (NULL for root ones)

private:
    XQOperator_e			m_eOp;			///< operation over childen
    int						m_iOrder;       // cache related
    int						m_iCounter;     // cache related.

private:
	mutable uint64_t		m_iMagicHash;

public:
	CSphVector<XQNode_t*>	m_dChildren;	///< non-plain node children
	XQLimitSpec_t			m_dSpec;		///< specification by field, zone(s), etc.

	CSphVector<XQKeyword_t>	m_dWords;		///< query words (plain node)
	int						m_iOpArg;		///< operator argument (proximity distance, quorum count)
	int						m_iAtomPos;		///< atom position override (currently only used within expanded nodes)
	bool					m_bVirtuallyPlain;	///< "virtually plain" flag (currently only used by expanded nodes)
	bool					m_bNotWeighted;	///< this our expanded but empty word's node

public:

    template <typename Writer>
    void Serialize(Writer& writer) const {
        // FIXME: add supported `expanded` word..
        writer.StartObject();

        writer.String("op");
        writer.String(GetOpTypeString(m_eOp));

        // the limit spec
        if(m_dSpec.m_bFieldSpec) {
            writer.String(("spec"));
            m_dSpec.Serialize(writer);
        }

        // child nodes.
        writer.String(("children"));
        writer.StartArray();
        ARRAY_FOREACH ( i, m_dChildren )
            m_dChildren[i]->Serialize(writer);
        writer.EndArray();

        // the words -> if is a plain node.
        writer.String(("words"));
        writer.StartArray();
        ARRAY_FOREACH ( i, m_dWords )
            m_dWords[i].Serialize(writer);
        writer.EndArray();


        writer.String("oparg");
        writer.Uint(m_iOpArg);

        // expanded related.
        //writer.String("op");
        writer.EndObject();
    }



    const char* GetOpTypeString(XQOperator_e eOp)  const{
        switch(eOp) {
        case SPH_QUERY_AND:  return "and";
        case SPH_QUERY_OR:  return "or";
        case SPH_QUERY_NOT:  return "not";
        case SPH_QUERY_ANDNOT:  return "andnot"; //and query with not postfix term.
        case SPH_QUERY_BEFORE:  return "before";
        case SPH_QUERY_PHRASE:  return "phrase";
        case SPH_QUERY_PROXIMITY:  return "proximity";
        case SPH_QUERY_QUORUM:  return "quorum";
        case SPH_QUERY_NEAR:  return "near";
        case SPH_QUERY_SENTENCE:  return "sentence";
        case SPH_QUERY_PARAGRAPH:  return "paragraph";
        }
        return NULL;
    }

public:
	/// ctor
	explicit XQNode_t ( const XQLimitSpec_t & dSpec )
		: m_pParent ( NULL )
		, m_eOp ( SPH_QUERY_AND )
		, m_iOrder ( 0 )
		, m_iCounter ( 0 )
		, m_iMagicHash ( 0 )
		, m_dSpec ( dSpec )
		, m_iOpArg ( 0 )
		, m_iAtomPos ( -1 )
		, m_bVirtuallyPlain ( false )
		, m_bNotWeighted ( false )
	{}

	/// dtor
	~XQNode_t ()
	{
		ARRAY_FOREACH ( i, m_dChildren )
			SafeDelete ( m_dChildren[i] );
	}

	/// check if i'm empty
	bool IsEmpty () const
	{
		assert ( m_dWords.GetLength()==0 || m_dChildren.GetLength()==0 );
		return m_dWords.GetLength()==0 && m_dChildren.GetLength()==0;
	}

	/// setup field limits
	void SetFieldSpec ( const CSphSmallBitvec& uMask, int iMaxPos );

	/// setup zone limits
	void SetZoneSpec ( const CSphVector<int> & dZones );

	/// copy field/zone limits from another node
	void CopySpecs ( const XQNode_t * pSpecs );

	/// unconditionally clear field mask
	void ClearFieldMask ();

public:
	/// get my operator
	XQOperator_e GetOp () const
	{
		return m_eOp;
	}

	/// get my cache order
	DWORD GetOrder () const
	{
		return m_iOrder;
	}

	/// get my cache counter
	int GetCount () const
	{
		return m_iCounter;
	}

	/// setup common nodes for caching
	void TagAsCommon ( int iOrder, int iCounter )
	{
		m_iCounter = iCounter;
		m_iOrder = iOrder;
	}

	/// precise comparison
	bool IsEqualTo ( const XQNode_t * pNode );

	/// hash me
	uint64_t GetHash () const;

	/// setup new operator and args
	void SetOp ( XQOperator_e eOp, XQNode_t * pArg1, XQNode_t * pArg2=NULL );

	/// setup new operator and args
	void SetOp ( XQOperator_e eOp, CSphVector<XQNode_t*> & dArgs )
	{
		m_eOp = eOp;
		m_dChildren.SwapData(dArgs);
	}

	/// setup new operator (careful parser/transform use only)
	void SetOp ( XQOperator_e eOp )
	{
		m_eOp = eOp;
	}

#ifndef NDEBUG
	/// consistency check
	void Check ( bool bRoot )
	{
		assert ( bRoot || !IsEmpty() ); // empty leaves must be removed from the final tree; empty root is allowed
		assert (!( m_dWords.GetLength() && m_eOp!=SPH_QUERY_AND && m_eOp!=SPH_QUERY_PHRASE && m_eOp!=SPH_QUERY_PROXIMITY && m_eOp!=SPH_QUERY_QUORUM )); // words are only allowed in these node types
		assert (!( m_dWords.GetLength()==1 && m_eOp!=SPH_QUERY_AND )); // 1-word leaves must be of AND type

		ARRAY_FOREACH ( i, m_dChildren )
			m_dChildren[i]->Check ( false );
	}
#endif
};


/// extended query
struct XQQuery_t : public ISphNoncopyable
{
    CSphString				m_sParseError;
	CSphString				m_sParseWarning;

	CSphVector<CSphString>	m_dZones;
	XQNode_t *				m_pRoot;
	bool					m_bSingleWord;

    bool                    m_bLoadFromCache; // by coreseek caching.
	/// ctor
	XQQuery_t ()
	{
		m_pRoot = NULL;
		m_bSingleWord = false;
	}

	/// dtor
	~XQQuery_t ()
	{
		SafeDelete ( m_pRoot );
	}

    // coreseek json query.
public:
    template <typename Writer>
    void Serialize(Writer& writer) const {
        writer.StartObject();

        // NOT write SingleWord, this attribute calc dyn.
        //writer.String("SingleWord");
        //writer.Bool(m_bSingleWord);

        if(m_dZones.GetLength()) {
            writer.String(("zones"));
            writer.StartArray();
            ARRAY_FOREACH ( i, m_dZones )
                writer.String(m_dZones[i].cstr(), (rapidjson::SizeType)m_dZones[i].Length());
            writer.EndArray();
        }

        if(m_pRoot) {
            writer.String("node");
            m_pRoot->Serialize(writer);
        }
        writer.EndObject();

        if(m_pRoot)
           printf("hash %ld\n", m_pRoot->GetHash());
    }

    int LoadJson(const char* json_ctx );
};

//////////////////////////////////////////////////////////////////////////////

/// parses the query and returns the resulting tree
/// return false and fills tQuery.m_sParseError on error
/// WARNING, parsed tree might be NULL (eg. if query was empty)
bool	sphParseExtendedQuery ( XQQuery_t & tQuery, const char * sQuery, const ISphTokenizer * pTokenizer, const CSphSchema * pSchema, CSphDict * pDict, int iStopwordStep );

/// analyse vector of trees and tag common parts of them (to cache them later)
int		sphMarkCommonSubtrees ( int iXQ, const XQQuery_t * pXQ );

#endif // _sphinxquery_

//
// $Id$
//
