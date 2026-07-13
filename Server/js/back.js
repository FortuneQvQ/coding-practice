let params=new URLSearchParams(window.location.search);
let from=params.get("from");

let html="";
if(from=="index"){
    html +=
    `
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="time_today"){
    html +=
    `
    <a class="button" href="../categories/time_today.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="time_weekago"){
    html +=
    `
    <a class="button" href="../categories/time_weekago.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="time_monthago"){
    html +=
    `
    <a class="button" href="../categories/time_monthago.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="source_jwc"){
    html +=
    `
    <a class="button" href="../categories/source_jwc.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="source_xsh"){
    html +=
    `
    <a class="button" href="../categories/source_xsh.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="source_xy"){
    html +=
    `
    <a class="button" href="../categories/source_xy.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="source_club"){
    html +=
    `
    <a class="button" href="../categories/source_club.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="topic_activity"){
    html +=
    `
    <a class="button" href="../categories/topic_activity.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="topic_competition"){
    html +=
    `
    <a class="button" href="../categories/topic_competition.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="topic_exam"){
    html +=
    `
    <a class="button" href="../categories/topic_exam.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="topic_research"){
    html +=
    `
    <a class="button" href="../categories/topic_research.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="topic_teach"){
    html +=
    `
    <a class="button" href="../categories/topic_teach.html">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}

else if(from=="search"){
    html +=
    `
    <a class="button" href="../search.html">返回搜索结果</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}
document.getElementById("bottom-buttons").innerHTML = html;