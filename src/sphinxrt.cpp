//
// $Id$
//

#include "sphinx.h"
#include "sphinxint.h"
#include "sphinxrt.h"

//////////////////////////////////////////////////////////////////////////

struct CmpHit_fn
{
	inline int operator () ( const CSphWordHit & a, const CSphWordHit & b )
	{
		return 	( a.m_iWordID < b.m_iWordID ) ||
			( a.m_iWordID == b.m_iWordID && a.m_iDocID < b.m_iDocID ) || 
			( a.m_iWordID == b.m_iWordID && a.m_iDocID == b.m_iDocID && a.m_iWordPos < b.m_iWordPos );
	}
};


struct RtDoc_t
{
	SphDocID_t					m_uDocID;	///< my document id
	DWORD						m_uFields;	///< fields mask
	DWORD						m_uHits;	///< hit count
	DWORD						m_uHit;		///< either index into segment hits, or the only hit itself (if hit count is 1)
};


struct RtWord_t
{
	SphWordID_t					m_uWordID;	///< my keyword id
	DWORD						m_uDocs;	///< document count (for stats and/or BM25)
	DWORD						m_uHits;	///< hit count (for stats and/or BM25)
	DWORD						m_uDoc;		///< index into segment docs
};


struct RtSegment_t
{
	CSphVector<RtWord_t>		m_dWords;
	CSphVector<RtDoc_t>			m_dDocs;
	CSphVector<DWORD>			m_dHits;

	int GetSize() const
	{
		return
			m_dWords.GetLength()*sizeof(m_dWords[0]) +
			m_dDocs.GetLength()*sizeof(m_dDocs[0]) +
			m_dHits.GetLength()*sizeof(m_dHits[0]);
	}
};


struct RtIndex_t : public ISphRtIndex
{
	CSphVector<CSphWordHit>		m_dAccum;
	CSphVector<RtSegment_t*>	m_pSegments;

	explicit					RtIndex_t ();
	virtual						~RtIndex_t ();

	void						AddDocument ( const CSphVector<CSphWordHit> & dHits );
	void						Commit ();

	RtSegment_t *				CreateSegment ();
	RtSegment_t *				MergeSegments ( const RtSegment_t * pSeg1, const RtSegment_t * pSeg2 );
	void						CopyWord ( RtSegment_t * pDst, const RtSegment_t * pSrc, const RtWord_t * pWord );
	void						MergeWord ( RtSegment_t * pDst, const RtSegment_t * pSrc1, const RtWord_t * pWord1, const RtSegment_t * pSrc2, const RtWord_t * pWord2 );
	void						CopyDoc ( RtSegment_t * pSeg, RtWord_t * pWord, const RtSegment_t * pSrc, const RtDoc_t * pDoc );

	void						DumpToDisk ( const char * sFilename );
};


RtIndex_t::RtIndex_t ()
{
	m_dAccum.Reserve ( 2*1024*1024 );
}


RtIndex_t::~RtIndex_t ()
{
	ARRAY_FOREACH ( i, m_pSegments )
		SafeDelete ( m_pSegments[i] );
}


void RtIndex_t::AddDocument ( const CSphVector<CSphWordHit> & dHits )
{
	ARRAY_FOREACH ( i, dHits )
		m_dAccum.Add ( dHits[i] );
}


RtSegment_t * RtIndex_t::CreateSegment ()
{
	RtSegment_t * pSeg = new RtSegment_t ();

	CSphWordHit tClosingHit;
	tClosingHit.m_iWordID = 0xffffffffUL; // !COMMIT: WORDID_MAX
	tClosingHit.m_iDocID = DOCID_MAX;
	tClosingHit.m_iWordPos = 1;
	m_dAccum.Add ( tClosingHit );
	m_dAccum.Sort ( CmpHit_fn() );

	RtDoc_t tDoc;
	tDoc.m_uDocID = 0;
	tDoc.m_uFields = 0;
	tDoc.m_uHits = 0;
	tDoc.m_uHit = 0;

	RtWord_t tWord;
	tWord.m_uWordID = 0;
	tWord.m_uDocs = 0;
	tWord.m_uHits = 0;
	tWord.m_uDoc = 0;

	ARRAY_FOREACH ( i, m_dAccum )
	{
		const CSphWordHit & tHit = m_dAccum[i];

		// new keyword or doc; flush current doc
		if ( tHit.m_iWordID!=tWord.m_uWordID || tHit.m_iDocID!=tDoc.m_uDocID )
		{
			if ( tDoc.m_uDocID )
			{
				tWord.m_uDocs++;
				tWord.m_uHits += tDoc.m_uHits;

// !COMMIT do this optimization
//				if ( tDoc.m_uHits==1 )
//					tDoc.m_uHit = pSeg->m_dHits.Pop();


				pSeg->m_dDocs.Add ( tDoc );
				tDoc.m_uFields = 0;
				tDoc.m_uHits = 0;
				tDoc.m_uHit = pSeg->m_dHits.GetLength();
			}
			tDoc.m_uDocID = tHit.m_iDocID;
		}

		// new keyword; flush current keyword
		if ( tHit.m_iWordID!=tWord.m_uWordID )
		{
			if ( tWord.m_uWordID )
				pSeg->m_dWords.Add ( tWord );

			tWord.m_uWordID = tHit.m_iWordID;
			tWord.m_uDocs = 0;
			tWord.m_uHits = 0;
			tWord.m_uDoc = pSeg->m_dDocs.GetLength();
		}

		// just a new hit
		pSeg->m_dHits.Add ( tHit.m_iWordPos );
		tDoc.m_uFields |= 1UL << ( tHit.m_iWordPos>>24 ); // !COMMIT HIT2LCS()
		tDoc.m_uHits++;
	}

	m_dAccum.Resize ( 0 );

	return pSeg;
}


void RtIndex_t::CopyWord ( RtSegment_t * pDst, const RtSegment_t * pSrc, const RtWord_t * pWord )
{
	assert ( pDst->m_dWords.GetLength()==0 || pWord->m_uWordID > pDst->m_dWords.Last().m_uWordID );

	pDst->m_dWords.Add ( *pWord );
	pDst->m_dWords.Last().m_uDoc = pDst->m_dDocs.GetLength();

	DWORD uHit = pDst->m_dHits.GetLength();
	for ( DWORD i=pWord->m_uDoc; i<pWord->m_uDoc+pWord->m_uDocs; i++ )
	{
		RtDoc_t & tDoc = pDst->m_dDocs.Add ();
		tDoc = pSrc->m_dDocs[i];
		tDoc.m_uHit = uHit;
		uHit += tDoc.m_uHits;
	}

	const RtDoc_t & tLastDoc = pSrc->m_dDocs [ pWord->m_uDoc + pWord->m_uDocs - 1 ];
	DWORD uStartHit = pSrc->m_dDocs [ pWord->m_uDoc ].m_uHit;
	DWORD uEndHit = tLastDoc.m_uHit + tLastDoc.m_uHits;
	for ( DWORD i=uStartHit; i<uEndHit; i++ )
		pDst->m_dHits.Add ( pSrc->m_dHits[i] );
}


void RtIndex_t::CopyDoc ( RtSegment_t * pSeg, RtWord_t * pWord, const RtSegment_t * pSrc, const RtDoc_t * pDoc )
{
	pWord->m_uDocs++;
	pWord->m_uHits += pDoc->m_uHits;

	RtDoc_t & tDoc = pSeg->m_dDocs.Add ();
	tDoc = *pDoc;
	tDoc.m_uHit = pSeg->m_dHits.GetLength();

	for ( DWORD i=pDoc->m_uHit; i<pDoc->m_uHit+pDoc->m_uHits; i++ )
		pSeg->m_dHits.Add ( pSrc->m_dHits[i] );
}


void RtIndex_t::MergeWord ( RtSegment_t * pSeg, const RtSegment_t * pSrc1, const RtWord_t * pWord1, const RtSegment_t * pSrc2, const RtWord_t * pWord2 )
{
	assert ( pWord1->m_uWordID==pWord2->m_uWordID );
	assert ( pSeg->m_dWords.GetLength()==0 || pWord1->m_uWordID > pSeg->m_dWords.Last().m_uWordID );

	RtWord_t & tWord = pSeg->m_dWords.Add ();
	tWord.m_uWordID = pWord1->m_uWordID;
	tWord.m_uDocs = 0;
	tWord.m_uHits = 0;
	tWord.m_uDoc = pSeg->m_dDocs.GetLength();

	const RtDoc_t * pDocs1 = &pSrc1->m_dDocs [ pWord1->m_uDoc ];
	const RtDoc_t * pDocs2 = &pSrc2->m_dDocs [ pWord2->m_uDoc ];
	const RtDoc_t * pDocsMax1 = pDocs1 + pWord1->m_uDocs;
	const RtDoc_t * pDocsMax2 = pDocs2 + pWord2->m_uDocs;

	for ( ;; )
	{
		// copy non-matching docs
		while ( pDocs1<pDocsMax1 && pDocs2<pDocsMax2 && pDocs1->m_uDocID!=pDocs2->m_uDocID )
			if ( pDocs1->m_uDocID < pDocs2->m_uDocID )
				CopyDoc ( pSeg, &tWord, pSrc1, pDocs1++ );
			else
				CopyDoc ( pSeg, &tWord, pSrc2, pDocs2++ );

		if ( pDocs1>=pDocsMax1 || pDocs2>=pDocsMax2 )
			break;

		// merge matching docs
		assert ( pDocs1<pDocsMax1 && pDocs2<pDocsMax2 && pDocs1->m_uDocID==pDocs2->m_uDocID );
		assert ( 0 );
		sphDie ( "!COMMIT not implemented yet" );
	}

	assert ( pDocs1>=pDocsMax1 || pDocs2>=pDocsMax2 );
	while ( pDocs1<pDocsMax1 ) CopyDoc ( pSeg, &tWord, pSrc1, pDocs1++ );
	while ( pDocs2<pDocsMax2 ) CopyDoc ( pSeg, &tWord, pSrc2, pDocs2++ );
}


RtSegment_t * RtIndex_t::MergeSegments ( const RtSegment_t * pSeg1, const RtSegment_t * pSeg2 )
{
	RtSegment_t * pSeg = new RtSegment_t ();

	const RtWord_t * pWords1 = &pSeg1->m_dWords[0];
	const RtWord_t * pWords2 = &pSeg2->m_dWords[0];
	const RtWord_t * pWordsMax1 = pWords1 + pSeg1->m_dWords.GetLength();
	const RtWord_t * pWordsMax2 = pWords2 + pSeg2->m_dWords.GetLength();

	// merge while there are common words
	for ( ;; )
	{
		while ( pWords1<pWordsMax1 && pWords2<pWordsMax2 && pWords1->m_uWordID!=pWords2->m_uWordID )
			if ( pWords1->m_uWordID < pWords2->m_uWordID )
				CopyWord ( pSeg, pSeg1, pWords1++ );
			else
				CopyWord ( pSeg, pSeg2, pWords2++ );

		if ( pWords1>=pWordsMax1 || pWords2>=pWordsMax2 )
			break;

		assert ( pWords1<pWordsMax1 && pWords2<pWordsMax2 && pWords1->m_uWordID==pWords2->m_uWordID );
		MergeWord ( pSeg, pSeg1, pWords1++, pSeg2, pWords2++ );
	}

	// copy tails
	while ( pWords1<pWordsMax1 ) CopyWord ( pSeg, pSeg1, pWords1++ );
	while ( pWords2<pWordsMax2 ) CopyWord ( pSeg, pSeg2, pWords2++ );
	return pSeg;
}


struct CmpSegments_fn
{
	inline int operator () ( const RtSegment_t * a, const RtSegment_t * b )
	{
		return a->GetSize() > b->GetSize();
	}
};


void RtIndex_t::Commit ()
{
	if ( !m_dAccum.GetLength() )
		return;

	CSphVector<RtSegment_t*> dSegments;
	dSegments = m_pSegments;
	dSegments.Add ( CreateSegment() );

	CSphVector<RtSegment_t*> dToKill;

	const int MAX_SEGMENTS = 8;
	for ( ;; )
	{
		dSegments.Sort ( CmpSegments_fn() );

		// unconditionally merge if there's too much segments now
		// conditionally merge if smallest segment has grown too large
		// otherwise, we're done
		int iLen = dSegments.GetLength();
		if (!( iLen>MAX_SEGMENTS || ( iLen>=2 && dSegments[iLen-1]->GetSize()*2 > dSegments[iLen-2]->GetSize() ) ))
			break;

		RtSegment_t * pA = dSegments.Pop();
		RtSegment_t * pB = dSegments.Pop();
		dSegments.Add ( MergeSegments ( pA, pB ) );
		dToKill.Add ( pA );
		dToKill.Add ( pB );
	}

	Swap ( m_pSegments, dSegments ); // !COMMIT atomic
	ARRAY_FOREACH ( i, dToKill )
		SafeDelete ( dToKill[i] ); // unused now
}


void RtIndex_t::DumpToDisk ( const char * sFilename )
{
	CSphString sName, sError;

	CSphWriter wrHits, wrDocs, wrDict;
	sName.SetSprintf ( "%s.spp", sFilename ); wrHits.OpenFile ( sName.cstr(), sError );
	sName.SetSprintf ( "%s.spd", sFilename ); wrDocs.OpenFile ( sName.cstr(), sError );
	sName.SetSprintf ( "%s.spi", sFilename ); wrDict.OpenFile ( sName.cstr(), sError );

	BYTE bDummy = 1;
	wrDict.PutBytes ( &bDummy, 1 );
	wrDocs.PutBytes ( &bDummy, 1 );
	wrHits.PutBytes ( &bDummy, 1 );

	float tmStart = sphLongTimer ();

	while ( m_pSegments.GetLength()>1 )
	{
		m_pSegments.Sort ( CmpSegments_fn() );
		RtSegment_t * pSeg1 = m_pSegments.Pop();
		RtSegment_t * pSeg2 = m_pSegments.Pop();
		m_pSegments.Add ( MergeSegments ( pSeg1, pSeg2 ) );
		SafeDelete ( pSeg1 );
		SafeDelete ( pSeg2 );
	}
	RtSegment_t * pSeg = m_pSegments[0];

	float tmMerged = sphLongTimer ();
	printf ( "final merge done in %.2f sec\n", tmMerged-tmStart );

	SphWordID_t uLastWord = 0;
	SphOffset_t uLastDocpos = 0;

	static const int WORDLIST_CHECKPOINT = 1024;
	int iWords = 0;

	struct Checkpoint_t
	{
		uint64_t m_uWord;
		uint64_t m_uOffset;
	};
	CSphVector<Checkpoint_t> dCheckpoints;

	ARRAY_FOREACH ( iWord, pSeg->m_dWords )
	{
		SphDocID_t uLastDoc = 0;
		SphOffset_t uLastHitpos = 0;
		const RtWord_t & tWord = pSeg->m_dWords[iWord];

		if ( !iWords )
		{
			Checkpoint_t & tChk = dCheckpoints.Add ();
			tChk.m_uWord = tWord.m_uWordID;
			tChk.m_uOffset = wrDict.GetPos();
		}

		wrDict.ZipInt ( tWord.m_uWordID - uLastWord );
		wrDict.ZipOffset ( wrDocs.GetPos() - uLastDocpos );
		wrDict.ZipInt ( tWord.m_uDocs );
		wrDict.ZipInt ( tWord.m_uHits );

		uLastDocpos = wrDocs.GetPos();
		uLastWord = tWord.m_uWordID;

		for ( DWORD uDoc = tWord.m_uDoc; uDoc < tWord.m_uDoc + tWord.m_uDocs; uDoc++ )
		{
			DWORD uLastHit = 0;
			const RtDoc_t & tDoc = pSeg->m_dDocs[uDoc];

			wrDocs.ZipOffset ( tDoc.m_uDocID-uLastDoc );
			wrDocs.ZipOffset ( wrHits.GetPos() - uLastHitpos );
			wrDocs.ZipInt ( tDoc.m_uFields );
			wrDocs.ZipInt ( tDoc.m_uHits );
			uLastDoc = tDoc.m_uDocID;
			uLastHitpos = wrHits.GetPos();

			for ( DWORD uHit = tDoc.m_uHit; uHit < tDoc.m_uHit + tDoc.m_uHits; uHit++ )
			{
				wrHits.ZipInt ( pSeg->m_dHits[uHit] - uLastHit );
				uLastHit = pSeg->m_dHits[uHit];
			}
			wrHits.ZipInt ( 0 );
		}
		wrDocs.ZipInt ( 0 );

		if ( ++iWords==WORDLIST_CHECKPOINT )
		{
			wrDict.ZipInt ( 0 );
			wrDict.ZipOffset ( wrDocs.GetPos() - uLastDocpos ); // store last hitlist length

			uLastDocpos = 0;
			uLastWord = 0;

			iWords = 0;
		}
	}
	wrDict.ZipInt ( 0 ); // indicate checkpoint
	wrDict.ZipOffset ( wrDocs.GetPos() - uLastDocpos ); // store last doclist length

	if ( dCheckpoints.GetLength() )
		wrDict.PutBytes ( &dCheckpoints[0], dCheckpoints.GetLength()*sizeof(Checkpoint_t) );

	wrHits.CloseFile ();
	wrDocs.CloseFile ();
	wrDict.CloseFile ();

	float tmDump = sphLongTimer ();
	printf ( "dump done in %.2f sec\n", tmDump-tmMerged );
}

//////////////////////////////////////////////////////////////////////////

ISphRtIndex * sphCreateIndexRT ()
{
	return new RtIndex_t ();
}

//
// $Id$
//
