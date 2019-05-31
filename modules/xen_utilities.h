#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <algorithm>

template<typename Cont, typename F>
inline void erase(Cont& c, F f)
{
	c.erase(std::remove_if(std::begin(c),
		std::end(c), f), std::end(c));
}

class EnvelopePoint
{
public:
	EnvelopePoint() {}
	EnvelopePoint(double x, double y) : m_x(x), m_y(y) {}
	double getX() const { return m_x; }
	double getY() const { return m_y; }
private:
	double m_x = 0.0;
	double m_y = 0.0;
};

struct ValueBetween
{
	double m_a = 0.0;
	double m_b = 0.0;
	ValueBetween(double a, double b) : m_a(a), m_b(b) {}
	bool operator()(EnvelopePoint& pt) const
	{
		return pt.getY() >= m_a && pt.getY() < m_b;
	}
};

class Envelope
{
public:
	Envelope()
	{}
	Envelope(std::initializer_list<EnvelopePoint> points) : m_points(points) 
	{
		sortPoints();
	}
	void clearAllPoints()
	{
		m_points.clear();
	}
	void addPoint(EnvelopePoint pt, bool nosort=false)
	{
		m_points.push_back(pt);
		if (nosort==false)
			sortPoints();
	}
	void addPoints(std::initializer_list<EnvelopePoint> pts)
	{
		for (const EnvelopePoint e : pts)
		{
			m_points.push_back(e);
		}
		sortPoints();
	}
	void removePoint(int index)
	{
		if (index>=0 && index<m_points.size())
			m_points.erase(m_points.begin() + index);
	}
	void removePointsInTimeRange(double t0, double t1)
	{
		erase(m_points, [t0, t1](const EnvelopePoint& a) { return a.getX() >= t0 && a.getX() <= t1; });
	}
	template<typename F>
	void removePointsConditionally(F f)
	{
		erase(m_points, f);
	}
	void sortPoints()
	{
		std::stable_sort(m_points.begin(), m_points.end(), [](const EnvelopePoint& a, const EnvelopePoint& b) 
		{
			return a.getX() < b.getX();
		});
	}
	// Not super efficient, if performance is really wanted, should implement a separate envelope playback object
	// that internally keeps track of the current point index etc...
	double getValueAtTime(double time) const
	{
		if (time < m_points.front().getX())
			return m_points.front().getY();
		if (time >= m_points.back().getX())
			return m_points.back().getY();
		int curnode = -1;
		const EnvelopePoint to_search(time, 0.0);
		auto it = std::lower_bound(m_points.begin(), m_points.end(), to_search,
			[](const EnvelopePoint& a, const EnvelopePoint& b)
		{ return a.getX() < b.getX(); });
		if (it == m_points.end())
		{
			return m_points.back().getY();
		}
		if (it != m_points.begin())
			--it; // lower_bound has returned iterator to point one too far
		curnode = std::distance(m_points.begin(), it);
		if (curnode >= 0)
		{
			auto pt0 = getPointSafe(curnode);
			auto pt1 = getPointSafe(curnode + 1);
			if (pt1.getX() - pt0.getX() < 0.00001)
				pt1 = { time,pt1.getY() };
			double v0 = pt0.getY();
			double v1 = pt1.getY();
			double deltanorm = jmap(time, pt0.getX(), pt1.getX(), 0.0, 1.0);
			return jmap(deltanorm, 0.0, 1.0, v0, v1);
		}
		return 0.0;
	}
	// Not necessarily very efficient either, but at least the starting envelope point
	// is searched for only once...
	void applyToBuffer(double* buf, int bufsize, double t0, double t1, Range<double> limitrange = {}) const
	{
		int curnode = -1;
		if (t0 >= m_points.front().getX())
		{
			const EnvelopePoint to_search(t0, 0.0);
			auto it = std::lower_bound(m_points.begin(), m_points.end(), to_search,
				[](const EnvelopePoint& a, const EnvelopePoint& b)
			{ return a.getX() < b.getX(); });
			if (it == m_points.end())
			{
				--it;
			}
			if (it != m_points.begin())
				--it; // lower_bound has returned iterator to point one too far
			curnode = std::distance(m_points.begin(), it);
		}
		auto pt0 = getPointSafe(curnode);
		auto pt1 = getPointSafe(curnode + 1);
		for (int i = 0; i < bufsize; ++i)
		{
			double v0 = pt0.getY();
			double v1 = pt1.getY();
			double time = jmap<double>(i, 0, bufsize, t0, t1);
			if (time >= pt1.getX())
			{
				++curnode;
				pt0 = getPointSafe(curnode);
				pt1 = getPointSafe(curnode + 1);
				v0 = pt0.getY();
				v1 = pt1.getY();
				if (pt1.getX() - pt0.getX() < 0.00001)
					pt1 = EnvelopePoint{ t1,v1 };
			}
			double deltanorm = jmap(time, pt0.getX(), pt1.getX(), 0.0, 1.0);
			buf[i] = jmap(deltanorm, 0.0, 1.0, v0, v1);
		}
		if (limitrange.isEmpty() == false)
		{
			for (int i = 0; i < bufsize; ++i)
			{
				buf[i] = jlimit(limitrange.getStart(), limitrange.getEnd(), buf[i]);
			}
		}
	}
	EnvelopePoint getPointSafe(int index) const
	{
		if (index < 0)
			return EnvelopePoint{ m_points.front().getX() - 0.1,m_points.front().getY() };
		if (index >= m_points.size())
			return EnvelopePoint{ m_points.back().getX(),m_points.back().getY() };
		return m_points[index];
	}
	void scaleTimes(double sx)
	{
		for (auto& e : m_points)
		{
			e = { e.getX()*sx,e.getY() };
		}
	}
	void scaleAndShiftValues(double sy, double shifty)
	{
		for (auto& e : m_points)
		{
			e = { e.getX(),sy*e.getY()+shifty };
		}
	}
private:
	std::vector<EnvelopePoint> m_points;
};

template<typename T>
inline T identity(T x)
{
	return x;
}

// Handy function to keep the CPU working... :-)
static int64_t CPU_waster(std::mt19937& rng, double durationtowaste)
{
	std::uniform_real_distribution<double> dist(-1.0, 1.0);
	std::atomic<double> acc{ 0.0 };
	int64_t loopcount = 0;
	double t0 = Time::getMillisecondCounterHiRes();
	while (true)
	{
		double v = dist(rng);
		double temp = acc.load();
		temp += v;
		acc.store(temp);
		++loopcount;
		double t1 = Time::getMillisecondCounterHiRes();
		if (t1 >= t0 + durationtowaste)
			break;
	}
	return loopcount;
}
