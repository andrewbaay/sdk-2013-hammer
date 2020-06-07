//========= Copyright Valve Corporation, All rights reserved. ============//
//
// $Revision: $
// $NoKeywords: $
//===========================================================================//

#ifndef UTLOBJECTREFERENCE_H
#define UTLOBJECTREFERENCE_H

#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlintrusivelist.h"
#include "mathlib/mathlib.h"

#pragma push_macro("GetObject")
#undef GetObject

// Purpose: class for keeping track of all the references that exist to an object.  When the object
// being referenced is freed, all of the references pointing at it will become null.
//
// To Use:
//   Add a DECLARE_REFERENCED_CLASS to the class that you want to use CutlReferences with.
//   Replace pointers to that class with CUtlReferences.
//   Check these references for null in appropriate places.
//
//  NOTE : You can still happily use pointers instead of references where you want to - these
//  pointers will not magically become null like references would, but if you know no one is going
//  to delete the underlying object during a partcular section of code, it doesn't
//  matter. Basically, CUtlReferences don't rely on every use of an object using one.




template<class T>
class CUtlReference
{
public:
	FORCEINLINE CUtlReference(void)
	{
		m_pNext = m_pPrev = NULL;
		m_pObject = NULL;
	}

	FORCEINLINE CUtlReference( T* pObj ) : CUtlReference()
	{
		AddRef( pObj );
	}

	FORCEINLINE CUtlReference( const CUtlReference<T>& other ) : CUtlReference()
	{
		if ( other.IsValid() )
		{
			AddRef( (T*)( other.GetObject() ) );
		}
	}

	FORCEINLINE ~CUtlReference(void)
	{
		KillRef();
	}

	FORCEINLINE void Set(T *pObj)
	{
		if ( m_pObject != pObj )
		{
			KillRef();
			AddRef( pObj );
		}
	}

	FORCEINLINE bool IsValid( void) const
	{
		return ( m_pObject != NULL );
	}

	FORCEINLINE T* operator()(void) const
	{
		return m_pObject;
	}

	FORCEINLINE operator T*() const
	{
		return m_pObject;
	}

	FORCEINLINE T* GetObject( void ) const
	{
		return m_pObject;
	}

	FORCEINLINE T* operator->() const
	{
		return m_pObject;
	}

	FORCEINLINE CUtlReference &operator=( const CUtlReference& otherRef )
	{
		Set( otherRef.m_pObject );
		return *this;
	}

	FORCEINLINE CUtlReference &operator=( T *pObj )
	{
		Set( pObj );
		return *this;
	}

	FORCEINLINE bool operator==( T const *pOther ) const
	{
		return ( pOther == m_pObject );
	}

	FORCEINLINE bool operator==( T *pOther ) const
	{
		return ( pOther == m_pObject );
	}

	FORCEINLINE bool operator==( const CUtlReference& o ) const
	{
		return ( o.m_pObject == m_pObject );
	}

	FORCEINLINE bool operator!=( T const *pOther ) const
	{
		return ( pOther != m_pObject );
	}

	FORCEINLINE bool operator!=( T *pOther ) const
	{
		return ( pOther != m_pObject );
	}

	FORCEINLINE bool operator!=( const CUtlReference& o ) const
	{
		return ( o.m_pObject != m_pObject );
	}

	FORCEINLINE bool operator!=( std::nullptr_t ) const
	{
		return IsValid();
	}

	FORCEINLINE bool operator==( std::nullptr_t ) const
	{
		return !IsValid();
	}

public:
	CUtlReference *m_pNext;
	CUtlReference *m_pPrev;

	T *m_pObject;

private:
	FORCEINLINE void AddRef( T *pObj )
	{
		m_pObject = pObj;
		if ( pObj )
		{
			pObj->m_References.AddToHead( this );
		}
	}

	FORCEINLINE void KillRef(void)
	{
		if ( m_pObject )
		{
			m_pObject->m_References.RemoveNode( this );
			m_pObject = NULL;
		}
	}

	template<typename T> friend class CUtlReferenceVector;
};

template<class T>
class CUtlReferenceList : public CUtlIntrusiveDList<CUtlReference<T>>
{
public:
	CUtlReferenceList()
	{
		RemoveAll();
	}

	~CUtlReferenceList( void )
	{
		CUtlReference<T> *i = CUtlIntrusiveDList<CUtlReference<T>>::m_pHead;
		while( i )
		{
			CUtlReference<T> *n = i->m_pNext;
			i->m_pNext = NULL;
			i->m_pPrev = NULL;
			i->m_pObject = NULL;
			i = n;
		}
		CUtlIntrusiveDList<CUtlReference<T> >::m_pHead = NULL;
	}
};


//-----------------------------------------------------------------------------
// Put this macro in classes that are referenced by CUtlReference
//-----------------------------------------------------------------------------
#define DECLARE_REFERENCED_CLASS( _className )				\
	private:												\
		CUtlReferenceList<_className> m_References;			\
		template<class T> friend class CUtlReference;


template <class T>
class CUtlReferenceVector : public CUtlBlockVector<CUtlReference<T>>
{
	using Base = CUtlBlockVector<CUtlReference<T>>;
public:

	void AddToTail( T* src )
	{
		Base::AddToTail( src );
	}

	void RemoveAll()
	{
		for ( int i = 0; i < Count(); i++ )
		{
			Base::Element( i ).KillRef();
		}

		Base::RemoveAll();
	}

	int Find( T* src ) const
	{
		return FindMatch( [&src]( const CUtlReference<T> & ref ) -> bool { return ref.GetObject() == src; } );
	}

	void FastRemove( int elem )
	{
		Assert( IsValidIndex( elem ) );

		if ( m_Size > 0 )
		{
			if ( elem != m_Size -1 )
			{
				Base::Element( elem ) = Base::Element( m_Size - 1 );
			}
			Destruct( &Base::Element( m_Size - 1 ) );
			--m_Size;
		}
	}

	bool FindAndFastRemove( T* src )
	{
		int elem = Find( src );
		if ( elem != -1 )
		{
			FastRemove( elem );
			return true;
		}
		return false;
	}

	void Remove( int elem )
	{
		Assert( IsValidIndex( elem ) );

		if ( m_Size > 0 )
		{
			for ( int i = elem; i < ( m_Size - 1 ); i++ )
			{
				Base::Element( i ) = Base::Element( i + 1 );
			}

			Destruct( &Base::Element( m_Size - 1 ) );
			--m_Size;
		}
	}

	bool FindAndRemove( T* src )
	{
		int elem = Find( src );
		if ( elem != -1 )
		{
			Remove( elem );
			return true;
		}
		return false;
	}

	T* operator[]( int i ) const
	{
		return Base::operator[]( i );
	}

	T* Element( int i ) const
	{
		return Base::Element( i );
	}

	template <typename TMatchFunc>
	int CountIf( TMatchFunc&& func ) const
	{
		int count = 0;
		for ( const auto& i : *this )
		{
			if ( func( i ) )
				++count;
		}
		return count;
	}

private:
	//
	// Disallow methods of CUtlBlockVector that can cause element addresses to change, thus
	// breaking assumptions of CUtlReference. If any of these becomes needed just add a safe
	// implementation to the public section.
	//
	void RemoveMultiple( int elem, int num );
	void RemoveMultipleFromHead(int num);
	void RemoveMultipleFromTail(int num);
	void Swap( CUtlReferenceVector< T > &vec );
	void Purge();
	void PurgeAndDeleteElements();
	void Compact();
};

#pragma pop_macro("GetObject")

#endif