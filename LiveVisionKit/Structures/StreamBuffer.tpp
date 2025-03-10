//    *************************** LiveVisionKit ****************************
//    Copyright (C) 2022  Sebastian Di Marco (crowsinc.dev@gmail.com)
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.
// 	  **********************************************************************

#pragma once

#include "Directives.hpp"

namespace lvk
{

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline StreamBuffer<T>::StreamBuffer(const size_t capacity)
		: m_Capacity(capacity)
	{
		LVK_ASSERT(capacity > 0);
		clear();
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline void StreamBuffer<T>::advance_window()
	{
		if(m_InternalBuffer.size() == capacity())
		{
			// If the internal buffer is full, then we are running as a circular queue.
			// However, It is still possible that the queue is not full, so ensure that
			// we only increase the size if we are not overstepping on the start index. 
			m_EndIndex = (m_EndIndex + 1) % m_Capacity;
			if(m_StartIndex == m_EndIndex)
            {
				m_StartIndex = (m_StartIndex + 1) % m_Capacity;
            }
			else m_Size++;
		}
		else
		{
			// If the internal buffer isn't full, then we must be zero-aligned
			m_EndIndex = m_InternalBuffer.size();
			m_Size++;
		}
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline void StreamBuffer<T>::push(const T& element)
	{
		advance_window();

		if(m_InternalBuffer.size() != m_Capacity)
			m_InternalBuffer.push_back(element);
		else
			m_InternalBuffer[m_EndIndex] = element;
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline void StreamBuffer<T>::push(T&& element)
	{
		advance_window();

		if(m_InternalBuffer.size() != m_Capacity)
			m_InternalBuffer.push_back(std::move(element));
		else
			m_InternalBuffer[m_EndIndex] = std::move(element);
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
	template<typename... Args>
    inline T& StreamBuffer<T>::advance(Args&&... args)
	{
		// Advances the window, either emplacing a new element or re-using the element
		// being overwritten when full. This exists in order to enable user-level
		// optimisations by removing the need to use the copying push function.

		advance_window();

		if(m_InternalBuffer.size() != m_Capacity)
			return m_InternalBuffer.emplace_back(args...);
		else
			return m_InternalBuffer[m_EndIndex];
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline void StreamBuffer<T>::skip(const size_t amount)
	{
		if(amount == 0)
			return;

        // Advances the start pointer to pop 'amount' elements from the front of the buffer.
        // This is the optimized counter-part to trim which does not de-allocate memory.

		if(amount >= m_Size)
		{
			// If the skip is clearing the buffer, or the buffer is already empty
			// then reset the buffer to be empty, but do not de-allocate memory. 
			m_StartIndex = 0;
			m_EndIndex = 0;
			m_Size = 0;
		}
		else
		{
			m_StartIndex = (m_StartIndex + amount) % m_Capacity;
			m_Size = m_Size - amount;
		}
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline void StreamBuffer<T>::trim(const size_t amount)
	{
		// Trimming elements from the front of the buffer, while also removing elements
        // from memory requires the circular queue to be zero-alligned. Simplest way to
        // do this is to allocate a new internal buffer and copy over all the untrimmed
        // elements. Note that trim(0) is effectively a zero-align operation.

		if(amount < m_Size)
		{
			const size_t new_size = m_Size - amount;

			std::vector<T> new_buffer;
			new_buffer.reserve(new_size);

			for (size_t i = 0; i < new_size; i++)
				if constexpr (std::is_move_constructible_v<T>)
					new_buffer.push_back(std::move(at(amount + i)));
				else
					new_buffer.push_back(at(amount + i));

			m_InternalBuffer = std::move(new_buffer);
			
			m_StartIndex = 0;
			m_EndIndex = new_size - 1;
			m_Size = new_size;
		}
		else clear();
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline void StreamBuffer<T>::resize(const size_t new_capacity)
	{
		LVK_ASSERT(m_Capacity > 0);

		if(new_capacity == m_Capacity)
			return;

		// If the new capacity is less than the number of elements we have, 
		// then we need to ensure we keep the newest N elements. Regardless,
		// we need to unravel the circular queue to start at index 0 so that
		// future pushes to the internal buffer are positioned in the correct
		// location. Trim will perform this for us, even if we trim nothing. 
		trim(new_capacity < size() ? size() - new_capacity : 0);
		m_Capacity = new_capacity;
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline void StreamBuffer<T>::clear()
	{
		m_Size = 0;
		m_EndIndex = 0;
		m_StartIndex = 0;
		m_InternalBuffer.clear();
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline T& StreamBuffer<T>::at(const size_t index)
	{
		LVK_ASSERT(index < size());

		return m_InternalBuffer[(m_StartIndex + index) % m_Capacity];
	}

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline const T& StreamBuffer<T>::at(const size_t index) const
    {
        LVK_ASSERT(index < size());

        return m_InternalBuffer[(m_StartIndex + index) % m_Capacity];
    }

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline T& StreamBuffer<T>::operator[](const size_t index)
	{
		return at(index);
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline const T& StreamBuffer<T>::operator[](const size_t index) const
	{
		return at(index);
	}

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline T& StreamBuffer<T>::oldest(const int offset)
    {
        LVK_ASSERT(!is_empty());
        LVK_ASSERT(offset >= 0);

        return at(offset);
    }

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline const T& StreamBuffer<T>::oldest(const int offset) const
	{
		LVK_ASSERT(!is_empty());
		LVK_ASSERT(offset >= 0);

		return at(offset);
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline T& StreamBuffer<T>::centre(const int offset)
	{
		LVK_ASSERT(!is_empty());

		// NOTE: Gets lower centre for even sizing.
		return at(centre_index() + offset);
	}

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline const T& StreamBuffer<T>::centre(const int offset) const
    {
        LVK_ASSERT(!is_empty());

        // NOTE: Gets lower centre for even sizing.
        return at(centre_index() + offset);
    }

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline T& StreamBuffer<T>::newest(const int offset)
	{
		LVK_ASSERT(!is_empty());
		LVK_ASSERT(offset <= 0);

		return at((size() - 1) + offset);
	}

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline const T& StreamBuffer<T>::newest(const int offset) const
    {
        LVK_ASSERT(!is_empty());
        LVK_ASSERT(offset <= 0);

        return at((size() - 1) + offset);
    }

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline bool StreamBuffer<T>::is_full() const
	{
		return size() == capacity();
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline bool StreamBuffer<T>::is_empty() const
	{
		return size() == 0;
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline size_t StreamBuffer<T>::size() const
	{
		return m_Size;
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline size_t StreamBuffer<T>::capacity() const
	{
		return m_Capacity;
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
    inline size_t StreamBuffer<T>::centre_index() const
	{
		LVK_ASSERT(!is_empty());

		// NOTE: Gets lower centre index for even sizing.
		// NOTE: This is an external 0-N index to be used with SlidingBuffer at() and [] operations.
		return (size() - 1) / 2;
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
	template<typename K>
    inline T StreamBuffer<T>::convolve_at(const StreamBuffer<K>& kernel, const size_t index) const
	{
		LVK_ASSERT(!is_empty());
		LVK_ASSERT(!kernel.is_empty());
		LVK_ASSERT(size() >= kernel.size());

		const auto kernel_centre_index = kernel.centre_index();
		const auto kernel_size = kernel.size(), buffer_size = this->size();

		size_t elems = 0, buffer_offset = 0, kernel_offset = 0;
		if(index <= kernel_centre_index)
		{
			// If the convolution index is left of the kernel's centre, then the 
			// left side of the kernel is going to be clipped off. This done by 
			// offsetting the start of the convolution inside the kernel.
			kernel_offset = kernel_centre_index - index;
			elems = kernel_size - kernel_offset;
		}
		else
		{
			// If the convolution index is right of the kernel's centre, then we 
			// need to offset the buffer instead so the kernel centre is applied
			// at the given index. Note that it is possible for the kernel to be
			// either bigger than the buffer (clipping on the right) or smaller,
			// clipping the buffer instead. This is handled by the element count. 
			buffer_offset = index - kernel_centre_index;
			elems = std::min(buffer_size - buffer_offset, kernel_size);
		}

		// Perform the convolution
        T result = this->at(buffer_offset) * kernel.at(kernel_offset);
		for(size_t i = 1; i < elems; i++)
            result += this->at(i + buffer_offset) * kernel.at(i + kernel_offset);

		return result;
	}

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
	template<typename K>
    inline StreamBuffer<T> StreamBuffer<T>::convolve(const StreamBuffer<K>& kernel) const
	{
		LVK_ASSERT(!is_empty());
		LVK_ASSERT(!kernel.is_empty());
		LVK_ASSERT(size() >= kernel.size());

		StreamBuffer<T> buffer(capacity());
		for (size_t i = 0; i < size(); i++)
			buffer.push(convolve_at(kernel, i));

		return buffer;
	}

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    StreamBuffer<T>::iterator StreamBuffer<T>::begin()
    {
        return StreamBuffer<T>::iterator(
            &oldest(),
            std::make_pair(&oldest(), &newest()),
            std::make_pair(&m_InternalBuffer.front(), &m_InternalBuffer.back())
        );
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    StreamBuffer<T>::const_iterator StreamBuffer<T>::begin() const
    {
        return StreamBuffer<T>::const_iterator(
            &oldest(),
            std::make_pair(&oldest(), &newest()),
            std::make_pair(&m_InternalBuffer.front(), &m_InternalBuffer.back())
        );
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    StreamBuffer<T>::const_iterator StreamBuffer<T>::cbegin() const
    {
        return StreamBuffer<T>::const_iterator(
            &oldest(),
            std::make_pair(&oldest(), &newest()),
            std::make_pair(&m_InternalBuffer.front(), &m_InternalBuffer.back())
        );
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    StreamBuffer<T>::iterator StreamBuffer<T>::end()
    {
        // NOTE: The end iterator is a begin iterator on its second cycle.
        return StreamBuffer<T>::iterator(
            &oldest(),
            std::make_pair(&oldest(), &newest()),
            std::make_pair(&m_InternalBuffer.front(), &m_InternalBuffer.back()),
            1
        );
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    StreamBuffer<T>::const_iterator StreamBuffer<T>::end() const
    {
        // NOTE: The end iterator is a begin iterator on its second cycle.
        return StreamBuffer<T>::const_iterator(
            &oldest(),
            std::make_pair(&oldest(), &newest()),
            std::make_pair(&m_InternalBuffer.front(), &m_InternalBuffer.back()),
            1
        );
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    StreamBuffer<T>::const_iterator StreamBuffer<T>::cend() const
    {
        // NOTE: The end iterator is a begin iterator on its second cycle.
        return StreamBuffer<T>::const_iterator(
            &oldest(),
            std::make_pair(&oldest(), &newest()),
            std::make_pair(&m_InternalBuffer.front(), &m_InternalBuffer.back()),
            1
        );
    }

//---------------------------------------------------------------------------------------------------------------------

	template<typename T>
	inline std::ostream& operator<<(std::ostream& stream, const StreamBuffer<T>& buffer)
	{
		stream << '[';
		if(!buffer.is_empty())
		{
			stream << buffer.at(0);
			for (size_t i = 1; i < buffer.size(); i++)
				stream << ", " << buffer[i];
		}
		stream << ']';

		return stream;
	}

//---------------------------------------------------------------------------------------------------------------------

}
